// SPDX-License-Identifier: GPL-3.0-only
#include "audio/AudioEngine.h"
#include "sloop_engine.h"

namespace sloop
{
namespace
{
uint8_t levelOf (int vel) { return vel < 50 ? SE_LV_GHOST : vel < 80 ? SE_LV_SOFT : vel < 112 ? SE_LV_NORM : SE_LV_HARD; }
int microOf (int sixths) { return juce::jlimit (-32, 31, sixths * 64 / 6); }   // the engine's nudge: 1/64 of a step
constexpr int kAhead = 4;           // the steps written ahead of the playhead (a step nudged early, a slide's next)
} // namespace

AudioEngine::AudioEngine()
{
    queue.resize ((size_t) fifo.getTotalSize());
    midiBlock.ensureSize (8192);
    midiNoteTrack.fill (-1);
    midiIn.reset (44100.0);
    for (int t = 0; t < kTracks; ++t) { playingSlot[t] = -1; position[t] = -1; barStep[t] = -1; hits[t] = 0; stepPhase[t] = 0.0f; peak[t] = 0; }
    se_init();
    se_set_bpm (tempo);
    se_set_kit (kDefaultKit);
    for (int t = 1; t < kTracks; ++t)
        se_set_sound (engineTrack (t), kDefaultSounds[(size_t) t].engine, kDefaultSounds[(size_t) t].preset);
    for (int t = 0; t < kTracks; ++t) { se_set_length (engineTrack (t), kMaxSteps); publishParams (t); }
}

// ------------------------------------------------------------------ the message thread's side
void AudioEngine::post (Command&& c)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) queue[(size_t) s1] = std::move (c);
    else if (n2 > 0) queue[(size_t) s2] = std::move (c);
    else jassertfalse;                               // the audio thread is not running or far behind: dropped
    fifo.finishedWrite (n1 + n2);
}
void AudioEngine::play (bool on, bool count) { Command c; c.type = Type::play; c.a = on; c.b = count; post (std::move (c)); }
void AudioEngine::setBpm (int bpm) { Command c; c.type = Type::bpm; c.a = bpm; post (std::move (c)); }
void AudioEngine::setSwing (int amount) { Command c; c.type = Type::swing; c.a = amount; post (std::move (c)); }
void AudioEngine::setMetronome (bool on) { Command c; c.type = Type::metronome; c.a = on; post (std::move (c)); }
void AudioEngine::setKey (const Key& k) { Command c; c.type = Type::key; c.a = k.root; c.b = k.scale; post (std::move (c)); }
void AudioEngine::renumber (int from, int delta)
{
    Command c; c.type = Type::renumber; c.a = from; c.b = delta; post (std::move (c));
}
void AudioEngine::setFill (bool on) { Command c; c.type = Type::fill; c.a = on; post (std::move (c)); }
void AudioEngine::setParam (int track, int param, int value)
{
    Command c; c.type = Type::param; c.a = track; c.b = param; c.c = value; post (std::move (c));
}
void AudioEngine::setSound (int track, int engine, int preset)
{
    Command c; c.type = Type::sound; c.a = track; c.b = engine; c.c = preset; post (std::move (c));
}
void AudioEngine::setParams (int track, const std::array<int, kParamSlots>& values)
{
    Command c; c.type = Type::params; c.a = track; c.values = values; post (std::move (c));
}
void AudioEngine::setKit (int kit) { Command c; c.type = Type::kit; c.a = kit; post (std::move (c)); }
void AudioEngine::drumHit (int lane, int velocity)
{
    Command c; c.type = Type::hit; c.a = lane; c.b = velocity; post (std::move (c));
}
void AudioEngine::note (int track, int midi, bool on, int velocity)
{
    Command c; c.type = Type::note; c.a = track; c.b = midi; c.c = on ? juce::jlimit (1, 127, velocity) : 0; post (std::move (c));
}
bool AudioEngine::startRecording (const juce::File& file)
{
    stopRecording();
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    auto stream = file.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    auto* writer = wav.createWriterFor (stream.get(), deviceRate, 2, 24, {}, 0);
    if (writer == nullptr) return false;
    stream.release();                                    // (the writer owns it now)
    writerThread.startThread();
    threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, writerThread, 1 << 17);   // (~3 s of room)
    recordedFrames = 0;
    const juce::ScopedLock sl (writerLock);
    activeWriter = threaded.get();
    return true;
}
void AudioEngine::stopRecording()
{
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = nullptr;
    }
    threaded.reset();                                    // (the rest written, the file closed)
}

void AudioEngine::setLink (bool on)
{
    if (on && ! link.isEnabled()) link.enable (true);    // (Link adopts the session's tempo if there is one)
    if (! on) link.enable (false);
    Command c; c.type = Type::link; c.a = on; post (std::move (c));
}
void AudioEngine::setAudioClip (When when, int row, const ClipPlayer::Clip& clip)
{
    Command c; c.type = Type::audio; c.when = when; c.a = row; c.audio = clip; post (std::move (c));
}
void AudioEngine::preview (const ClipPlayer::Clip& clip, bool on)
{
    Command c; c.type = Type::preview; c.a = on; c.audio = clip; post (std::move (c));
}
void AudioEngine::setSolo (unsigned mask) { Command c; c.type = Type::solo; c.a = (int) mask; post (std::move (c)); }
void AudioEngine::setGlobal (int id, int value) { Command c; c.type = Type::global; c.a = id; c.b = value; post (std::move (c)); }
void AudioEngine::repeat (int track, int id, int velocity, int rate, bool on)
{
    Command c; c.type = Type::repeat; c.a = track; c.b = id; c.c = velocity; c.n = rate; c.when = on ? When::now : When::update;
    post (std::move (c));
}
void AudioEngine::setPlaylist (int track, When when, const std::vector<std::pair<int, const ClipData*>>& clips)
{
    Command c;
    c.type = Type::playlist;
    c.when = when;
    c.a = track;
    for (auto& [slot, data] : clips)
        if (data == nullptr || ! c.chain.add (slot, *data)) break;
    post (std::move (c));
}

// ------------------------------------------------------------------ the audio thread's side
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    outputLatency = (int64_t) (device->getOutputLatencyInSamples() * 1.0e6 / juce::jmax (1.0, deviceRate));
}
void AudioEngine::prepare (double sampleRate, int blockSize)
{
    deviceRate = sampleRate;
    midiIn.reset (sampleRate);
    staging.setSize (2, juce::jmax (16384, blockSize * 4));
    staged = 0;
    for (auto& i : interp) i.reset();
    deviceFrames = 0;
    link.resetFilter();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) apply (queue[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) apply (queue[(size_t) (s2 + i)]);
    fifo.finishedRead (n1 + n2);

    {   // MIDI in: the notes onto the track selected (the drums: their General MIDI notes)
        auto& midi = midiBlock;
        midi.clear();                                    // (the collector adds to it: last block's out first)
        midiIn.removeNextBlockOfMessages (midi, numSamples);
        const int t = juce::jlimit (0, kTracks - 1, midiTrack.load());
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (! msg.isNoteOnOrOff()) continue;
            const int note = msg.getNoteNumber();
            if (! msg.isNoteOn())                        // a note off: to the track its note began on
            {
                if (const int on = midiNoteTrack[(size_t) note]; on >= 0) se_note_vel (engineTrack (on), note, 0);
                midiNoteTrack[(size_t) note] = -1;
            }
            else if (isDrum (t))
            {
                for (int lane = 0; lane < kLanes; ++lane)
                    if (se_lane_note (lane) == note) { se_drum_hit (lane, msg.getVelocity()); break; }
            }
            else
            {
                se_note_vel (engineTrack (t), note, msg.getVelocity());
                midiNoteTrack[(size_t) note] = (int8_t) t;
            }
        }
    }
    if (linkOn) callbackTime = link.frameTime (deviceFrames) + outputLatency;
    deviceFrames += numSamples;
    const double ratio = (double) se_sample_rate() / deviceRate;
    const int needed = (int) std::ceil (numSamples * ratio) + 4;
    while (staged < needed && staged + (int) se_block_frames() <= staging.getNumSamples())
        renderEngineBlock();

    int used = 0;
    const float gain = masterGain.load (std::memory_order_relaxed);
    float pk = masterPeak.load (std::memory_order_relaxed);
    for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
        if (out[ch] != nullptr)
        {
            used = interp[ch].process (ratio, staging.getReadPointer (ch), out[ch], numSamples);
            if (gain != 1.0f) juce::FloatVectorOperations::multiply (out[ch], gain, numSamples);
            pk = juce::jmax (pk, juce::FloatVectorOperations::findMaximum (out[ch], numSamples),
                             -juce::FloatVectorOperations::findMinimum (out[ch], numSamples));
        }
    masterPeak.store (pk, std::memory_order_relaxed);
    if (numOut >= 2 && out[0] != nullptr && out[1] != nullptr)
    {
        const juce::ScopedLock sl (writerLock);         // (taken only while starting or stopping)
        if (auto* w = activeWriter.load())
            if (w->write (out, numSamples)) recordedFrames += numSamples;
    }
    if (numOut == 1 && out[0] != nullptr)            // a mono device: the left channel only, kept simple
        for (int i = 0; i < numSamples; ++i) out[0][i] *= 0.7f;
    for (int ch = 2; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);

    used = juce::jlimit (0, staged, used);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = staging.getWritePointer (ch);
        std::memmove (d, d + used, sizeof (float) * (size_t) (staged - used));
    }
    staged -= used;
}


void AudioEngine::renderEngineBlock()
{
    const int frames = (int) se_block_frames();
    if (linkOn)                                          // (the block is heard after what is staged already)
    {
        const double us = 1.0e6 / se_sample_rate();
        followLink (callbackTime + (int64_t) (staged * us), callbackTime + (int64_t) ((staged + frames) * us), frames);
    }
    const bool moving = running.load (std::memory_order_relaxed);
    const double beats = ! moving ? 0.0 : linkOn ? linkMoved : frames * tempo / (60.0 * se_sample_rate());
    const float* o = se_render();
    auto* l = staging.getWritePointer (0, staged);
    auto* r = staging.getWritePointer (1, staged);
    const float engineGain = engineSilent ? 0.0f : 1.0f;   // (only the audio track soloed)
    for (int i = 0; i < frames; ++i) { l[i] = o[2 * i] * engineGain; r[i] = o[2 * i + 1] * engineGain; }
    renderAudioTrack (l, r, frames, beats);
    staged += frames;
    runRepeats (frames);
    for (int t = 0; t < kTracks; ++t)                    // (the meters: the loudest since the UI looked)
    {
        const int p = se_track_peak (engineTrack (t));
        if (p > peak[t].load (std::memory_order_relaxed)) peak[t].store (p, std::memory_order_relaxed);
    }
    if (countingIn)
    {
        // four beats of clicks at the tempo, then PLAY (the first click came with the command)
        const double beat = (double) se_sample_rate() * 60.0 / tempo;
        const int before = (int) (countFrames / beat);
        countFrames += frames;
        const int after = (int) (countFrames / beat);
        if (after >= 4) { countingIn = false; countIn = 0; startTransport(); }
        else if (after != before) { se_click (0); countIn = 4 - after; }
        return;
    }
    if (! running.load (std::memory_order_relaxed)) return;

    const float stepFrames = (float) se_sample_rate() * 15.0f / (float) tempo;   // a 1/16 step
    for (int t = 0; t < kTracks; ++t)
    {
        auto& s = tracks[(size_t) t];
        const int pos = se_position (engineTrack (t));
        if (pos == s.lastPos || pos < 0)
        {
            framesInStep[(size_t) t] += frames;
            stepPhase[t].store (juce::jlimit (0.0f, 1.0f, (float) framesInStep[(size_t) t] / stepFrames), std::memory_order_relaxed);
            continue;
        }
        framesInStep[(size_t) t] = 0;
        stepPhase[t].store (0.0f, std::memory_order_relaxed);
        const int64_t x = s.lastPos < 0 ? pos : s.cur + (pos - s.lastPos + kMaxSteps) % kMaxSteps;
        s.lastPos = pos;
        // the steps behind let their locks go (the ones in force stay until the next step), then what plays now,
        // then the step that comes into the window ahead
        for (int64_t y = juce::jmax ((int64_t) 0, s.cur, x - kAhead); y < x; ++y)
            se_set_step_locks (engineTrack (t), (int) (y % kMaxSteps), nullptr, 0);
        commit (t, x);
        for (int64_t y = juce::jmax (s.written + 1, x + 1); y <= x + kAhead; ++y) writeStep (t, y);
        s.written = juce::jmax (s.written, x + kAhead);
    }
}

void AudioEngine::commit (int t, int64_t step)
{
    auto& s = tracks[(size_t) t];
    const auto w = scheduler::locate (s, step);
    scheduler::commit (s, w, step);
    barStep[t] = (int) (step % kBar);
    const auto* clip = scheduler::clipOf (s, { s.idx, s.start, s.passes, false });
    if (w.idx < 0 || clip == nullptr)
    {
        playingSlot[t] = -1;
        position[t] = -1;
        return;
    }
    const int cs = (int) (step - s.start);
    playingSlot[t] = s.playingRow();
    position[t] = cs;
    if (clip->columnActive (cs)) hits[t].fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::rewrite (int t)
{
    auto& s = tracks[(size_t) t];
    if (! running) return;
    for (int64_t y = s.cur + 1; y <= s.written; ++y) writeStep (t, y);
}

void AudioEngine::apply (Command& c)
{
    switch (c.type)
    {
        case Type::play:
            if (linkOn) { linkPlayReq = c.a ? 1 : -1; break; }   // (the session starts and stops it: followLink)
            if (c.a && ! running && ! countingIn)
            {
                if (c.b) { countingIn = true; countFrames = 0; countIn = 4; se_click (1); }
                else startTransport();
            }
            else if (! c.a && (running || countingIn)) stopTransport();
            break;
        case Type::bpm:
            tempo = c.a;
            se_set_bpm (c.a);
            if (linkOn) linkTempoReq = c.a;
            break;
        case Type::link:
            linkOn = c.a != 0;
            if (linkOn)
            {
                seenTempo = 0;                           // (the session's state published at the next block)
                linkTempoReq = link.peers() == 0 ? tempo : 0;   // alone: ours; else the session's (no hijack)
                if (running) { linkPlayReq = 1; linkOrigin = 0; linkBeat = 0; }
                link.resetFilter();
                deviceFrames = 0;
            }
            else se_set_advance (-1);
            break;
        case Type::swing: se_set_swing (c.a); break;
        case Type::metronome: se_set_metronome (c.a); break;
        case Type::key:
            key = { c.a, c.b };
            for (int t = 0; t < kTracks; ++t) rewrite (t);
            break;
        case Type::fill:
            fill = c.a != 0;
            se_set_fill (0);                         // the app resolves the fill conditions itself (per drum sound)
            for (int t = 0; t < kTracks; ++t) rewrite (t);
            break;
        case Type::param: se_set_param (engineTrack (c.a), c.b, c.c); break;
        case Type::sound: se_set_sound (engineTrack (c.a), c.b, c.c); publishParams (c.a); break;
        case Type::params:
            for (int id = 0; id < se_param_count(); ++id)
                if (! (isDrum (c.a) && id == SE_P_E0)) se_set_param (engineTrack (c.a), id, c.values[(size_t) id]);
            publishParams (c.a);
            break;
        case Type::kit: se_set_kit (c.a); break;
        case Type::hit: se_drum_hit (c.a, c.b); break;
        case Type::note: se_note_vel (engineTrack (c.a), c.b, c.c); break;
        case Type::repeat:
        {
            Repeat* slot = nullptr;
            for (auto& r : repeats) if (r.track == c.a && r.id == c.b) slot = &r;
            if (c.when == When::update)                  // let go: the last note ends
            {
                if (slot != nullptr)
                {
                    if (! isDrum (c.a) && slot->off >= 0) se_note (engineTrack (c.a), c.b, 0);
                    *slot = {};
                }
                break;
            }
            if (slot == nullptr) for (auto& r : repeats) if (r.track < 0) { slot = &r; break; }
            if (slot == nullptr) break;
            const double step = (double) se_sample_rate() * 15.0 / tempo, interval = step * repeatSteps (c.n);
            double next = clock + interval;              // (the first hit is the pad's own)
            if (running)                                 // on the grid: the next multiple of the rate since PLAY
            {
                const auto& s = tracks[(size_t) c.a];
                const double since = (double) juce::jmax ((int64_t) 0, s.cur) * step + framesInStep[(size_t) c.a];
                next = clock + (std::floor (since / interval) + 1.0) * interval - since;
            }
            *slot = { c.a, c.b, c.c, interval, next, isDrum (c.a) ? -1.0 : clock + interval * 0.5 };
            break;
        }
        case Type::solo:
        {
            unsigned m = 0;                              // (the app's tracks -> the engine's)
            for (int t = 0; t < kTracks; ++t) if ((unsigned) c.a >> t & 1u) m |= 1u << engineTrack (t);
            se_set_solo (m);
            audioSolo = ((unsigned) c.a >> kAudio & 1u) != 0;
            engineSilent = audioSolo && m == 0;          // (the audio track alone)
            soloAny = c.a != 0;
            break;
        }
        case Type::global: se_set_global (c.a, c.b); break;
        case Type::audio:
            if (c.when == When::update)                  // the clip edited: where it plays, where it waits
            {
                if (audioRow == c.a) { audioNow = c.audio; player.update (c.audio); }
                if (audioPending && audioNextRow == c.a) audioNext = c.audio;
            }
            else if (c.when == When::nextBar && running)
            {
                audioPending = true;
                audioNextRow = c.a;
                audioNext = c.audio;
                audioAt = (std::floor (songBeat / 4.0 + 1.0e-9) + 1.0) * 4.0;
            }
            else                                         // at once: from the start of the bar playing
            {
                audioPending = false;
                audioRow = c.a;
                audioNow = c.audio;
                if (audioRow < 0) player.stop();
                else player.start (audioNow, running ? std::floor (songBeat / 4.0 + 1.0e-9) * 4.0 : 0.0);
                audioSlot = audioRow;
            }
            break;
        case Type::preview:
            if (c.a) { previewPlayer.start (c.audio, 0.0); previewBeat = 0; }
            else { previewPlayer.stop(); previewProgress = -1.0f; }
            break;
        case Type::renumber:
            for (int t = 0; t < kTracks; ++t)
            {
                auto& s = tracks[(size_t) t];
                for (auto* ch : { &s.chain, &s.next })
                    for (int i = 0; i < ch->n; ++i) if (ch->rows[(size_t) i] >= c.a) ch->rows[(size_t) i] += c.b;
                if (s.chain.n) playingSlot[t] = s.playingRow();
            }
            if (audioRow >= c.a) audioRow += c.b;
            if (audioNextRow >= c.a) audioNextRow += c.b;
            audioSlot = audioRow;
            break;
        case Type::playlist:
        {
            auto& s = tracks[(size_t) c.a];
            if (c.when == When::update)
            {
                for (int i = 0; i < c.chain.n; ++i)
                    for (auto* ch : { &s.chain, &s.next })
                        for (int j = 0; j < ch->n; ++j)
                            if (ch->rows[(size_t) j] == c.chain.rows[(size_t) i]) ch->clips[(size_t) j] = c.chain.clips[(size_t) i];
            }
            else if (c.when == When::nextBar && running)
            {
                s.next = c.chain;
                s.pending = true;
                s.pendingAt = s.cur < 0 ? 0 : (s.cur / kBar + 1) * kBar;
            }
            else
            {
                // at once: from the start of the bar playing (a clip recorded into from here lines up with it)
                s.chain = c.chain;
                s.idx = 0; s.passes = 0; s.pending = false;
                s.start = s.cur < 0 ? 0 : s.cur - s.cur % kBar;
                playingSlot[c.a] = s.playingRow();
            }
            rewrite (c.a);
            break;
        }
    }
}

// Link, before each engine block: what this app asked first (a tempo, PLAY / STOP), then the session's tempo and
// transport, then how far the song moves in the block: to the session's beat at its end (a bar of the session is a
// bar here), at no more than twice the tempo when behind (no burst), not at all when ahead
void AudioEngine::followLink (int64_t start, int64_t end, int frames)
{
    if (linkTempoReq > 0) { link.setTempo (linkTempoReq, start); linkTempoReq = 0; }
    if (linkPlayReq != 0) { link.setPlaying (linkPlayReq > 0, start); linkPlayReq = 0; }
    const auto st = link.capture (start, end);
    if (std::abs (st.tempo - seenTempo) > 1.0e-3 || st.playing != seenPlaying)
    {
        seenTempo = st.tempo;
        seenPlaying = st.playing;
        if (const int bpm = juce::jlimit (kMinBpm, kMaxBpm, juce::roundToInt (st.tempo)); bpm != tempo)
        {
            tempo = bpm;                                 // (the delays, the arps, the note repeat)
            se_set_bpm (bpm);
        }
        linkTempo = st.tempo;
        linkPlaying = st.playing;
        linkSerial.fetch_add (1);
    }
    if (st.playing && ! running.load (std::memory_order_relaxed))
    {
        const double bar = std::ceil (st.beatStart / LinkSync::kQuantum - 1.0e-9) * LinkSync::kQuantum;
        if (bar >= st.beatEnd) return;                   // the session's next bar not yet
        countingIn = false;
        countIn = 0;
        startTransport();
        linkOrigin = bar;
        linkBeat = 0;
    }
    else if (! st.playing && running.load (std::memory_order_relaxed))
    {
        stopTransport();
    }
    linkMoved = 0;
    if (! running.load (std::memory_order_relaxed)) return;
    constexpr double beatUnits = 44100.0 * 60.0;        // (the engine's clock: a sample at 1 BPM, firmware core.h)
    const double nominal = frames / (double) se_sample_rate() * st.tempo / 60.0;
    const double ahead = juce::jlimit (0.0, 2.0 * nominal, st.beatEnd - linkOrigin - linkBeat);
    const double moved = std::round (ahead * beatUnits) / beatUnits;
    se_set_advance (moved);
    linkBeat += moved;
    linkMoved = moved;
}

// the audio track over the block: its clip on the song's beats (the one waiting starts on its bar), at its level and
// pan; then the clip listened to alone, at its own pace
void AudioEngine::renderAudioTrack (float* l, float* r, int frames, double beats)
{
    frames = juce::jmin (frames, (int) mixL.size());
    std::fill (mixL.begin(), mixL.begin() + frames, 0.0f);
    std::fill (mixR.begin(), mixR.begin() + frames, 0.0f);
    if (running.load (std::memory_order_relaxed) && beats > 0.0)
    {
        if (audioPending && songBeat + beats > audioAt)
        {
            audioPending = false;
            audioRow = audioNextRow;
            audioNow = audioNext;
            if (audioRow < 0) player.stop();
            else player.start (audioNow, audioAt);
            audioSlot = audioRow;
        }
        player.render (songBeat, songBeat + beats, mixL.data(), mixR.data(), frames);
        songBeat += beats;
        audioProgress = (float) player.progress (songBeat);
    }
    const bool heard = ! audioMute.load (std::memory_order_relaxed) && (! soloAny || audioSolo);
    const float level = heard ? audioLevel.load (std::memory_order_relaxed) : 0.0f;
    const float pan = audioPan.load (std::memory_order_relaxed);
    const float gl = level * std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f, gr = level * std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
    float pk = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const float a = mixL[(size_t) i] * gl, b = mixR[(size_t) i] * gr;
        l[i] += a;
        r[i] += b;
        pk = juce::jmax (pk, std::abs (a), std::abs (b));
    }
    if (const int q = (int) (juce::jmin (1.0f, pk) * 32767.0f); q > audioPeak.load (std::memory_order_relaxed))
        audioPeak.store (q, std::memory_order_relaxed);
    if (previewPlayer.playing())                        // (at its own pace: a beat of it lasts its own beat)
    {
        const double b = frames * previewPlayer.beatsPerFrame();
        previewPlayer.render (previewBeat, previewBeat + b, l, r, frames);
        previewBeat += b;
        previewProgress = (float) previewPlayer.progress (previewBeat);
    }
}

void AudioEngine::runRepeats (int frames)
{
    clock += frames;
    for (auto& r : repeats)
    {
        if (r.track < 0) continue;
        if (! isDrum (r.track) && r.off >= 0 && clock >= r.off) { se_note (engineTrack (r.track), r.id, 0); r.off = -1; }
        if (clock < r.next) continue;
        if (isDrum (r.track)) se_drum_hit (r.id, r.vel);
        else { se_note (engineTrack (r.track), r.id, 1); r.off = clock + r.interval * 0.5; }   // (half the interval long)
        r.next += r.interval;
        if (r.next < clock) r.next = clock + r.interval;   // (a tempo change: no burst to catch up)
    }
}

void AudioEngine::publishParams (int t)
{
    int16_t p[kParamSlots] {};
    jassert (se_param_count() <= kParamSlots);
    se_params (engineTrack (t), p);
    for (int i = 0; i < kParamSlots; ++i) paramValue[t][i] = p[i];
    paramSerial.fetch_add (1);
}


void AudioEngine::startTransport()
{
    for (int t = 0; t < kTracks; ++t)
    {
        auto& s = tracks[(size_t) t];
        if (s.pending) s.takeNext();
        s.rewind();
        s.seed = rng = scheduler::hash (rng + 1);
        se_set_locks (engineTrack (t), nullptr, 0);   // (no lock left from the last time anywhere in the loop)
        for (int64_t y = 0; y <= kAhead; ++y) writeStep (t, y);
        s.written = kAhead;
        playingSlot[t] = s.playingRow();
        framesInStep[(size_t) t] = 0;
    }
    songBeat = 0;
    if (audioPending) { audioRow = audioNextRow; audioNow = audioNext; audioPending = false; audioSlot = audioRow; }
    if (audioRow >= 0) player.start (audioNow, 0.0);
    se_play (1);
    running = true;
}

void AudioEngine::stopTransport()
{
    se_play (0);
    running = false;
    countingIn = false;
    countIn = 0;
    if (audioPending) { audioRow = audioNextRow; audioNow = audioNext; audioPending = false; audioSlot = audioRow; }
    player.stop();
    audioProgress = 0.0f;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& s = tracks[(size_t) t];
        if (s.pending) s.takeNext();
        s.rewind();
        s.written = -1;
        playingSlot[t] = s.playingRow();
        position[t] = -1;
        barStep[t] = -1;
    }
}

// the engine's step for `step` (since PLAY): the step of the clip due there, as that pass plays it
void AudioEngine::writeStep (int t, int64_t step)
{
    const int et = engineTrack (t), e = (int) (step % kMaxSteps);
    const auto x = scheduler::resolve (tracks[(size_t) t], step, t, key, fill);
    if (isDrum (t))
    {
        uint8_t lv[kLanes] {};
        for (int lane = 0; lane < kLanes; ++lane) lv[lane] = levelOf (x.velocity[(size_t) lane]);
        se_drum_step (e, x.lanes, lv, x.ratchet.data(), microOf (x.nudge), SE_FILL_ANY);
    }
    else
    {
        se_synth_step_cont (et, e, x.notes.data(), x.nNotes, x.held, x.vel, x.ratchetCount, microOf (x.nudge), SE_FILL_ANY);
    }
    static_assert (std::tuple_size<decltype (x.locks)>::value == SE_MAX_LOCKS, "a step's locks: the engine's room");
    se_lock_t locks[SE_MAX_LOCKS];
    for (int i = 0; i < x.nLocks; ++i) locks[i] = { (uint8_t) e, x.locks[(size_t) i].param, x.locks[(size_t) i].value };
    se_set_step_locks (et, e, locks, x.nLocks);
}
} // namespace sloop
