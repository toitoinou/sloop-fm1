// SPDX-License-Identifier: GPL-3.0-only
#include "AudioEngine.h"
#include "sloop_engine.h"

namespace sloop
{
namespace
{
uint8_t levelOf (int vel) { return vel < 50 ? SE_LV_GHOST : vel < 80 ? SE_LV_SOFT : vel < 112 ? SE_LV_NORM : SE_LV_HARD; }
int microOf (int sixths) { return juce::jlimit (-32, 31, sixths * 64 / 6); }   // the engine's nudge: 1/64 of a step
constexpr int kAhead = 4;           // the steps written ahead of the playhead (a step nudged early, a slide's next)
uint32_t mix (uint32_t h)
{
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
} // namespace

AudioEngine::AudioEngine()
{
    queue.resize ((size_t) fifo.getTotalSize());
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
void AudioEngine::note (int track, int midi, bool on)
{
    Command c; c.type = Type::note; c.a = track; c.b = midi; c.c = on; post (std::move (c));
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
    threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, writerThread, 32768);
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
    {
        if (c.n == kChainMax || data == nullptr) break;
        c.slots[c.n] = slot;
        c.clips[c.n++] = *data;
    }
    post (std::move (c));
}

// ------------------------------------------------------------------ the audio thread's side
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}
void AudioEngine::prepare (double sampleRate, int blockSize)
{
    deviceRate = sampleRate;
    staging.setSize (2, juce::jmax (16384, blockSize * 4));
    staged = 0;
    for (auto& i : interp) i.reset();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) apply (queue[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) apply (queue[(size_t) (s2 + i)]);
    fifo.finishedRead (n1 + n2);

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
    const float* o = se_render();
    const int frames = (int) se_block_frames();
    auto* l = staging.getWritePointer (0, staged);
    auto* r = staging.getWritePointer (1, staged);
    for (int i = 0; i < frames; ++i) { l[i] = o[2 * i]; r[i] = o[2 * i + 1]; }
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

AudioEngine::Where AudioEngine::locate (const TrackState& s, int64_t step) const
{
    Where w { s.idx, s.start, s.passes, false };
    int n = s.n;
    const int* slots = s.slots;
    const ClipData* clips = s.clips;
    if (s.pending && step >= s.pendingAt)
    {
        n = s.next.n; slots = s.next.slots; clips = s.next.clips;
        w = { 0, s.pendingAt, 0, true };
    }
    if (n == 0) return { -1, step, 0, w.fromNext };
    // the chain goes on clip after clip (a single clip: pass after pass)
    for (int guard = 0; step - w.start >= clips[w.idx].length && guard < 4096; ++guard)
    {
        w.start += clips[w.idx].length;
        const int nx = (w.idx + 1) % n;
        w.passes = slots[nx] == slots[w.idx] ? w.passes + 1 : 0;
        w.idx = nx;
    }
    return w;
}

void AudioEngine::commit (int t, int64_t step)
{
    auto& s = tracks[(size_t) t];
    const auto w = locate (s, step);
    if (w.fromNext)
    {
        s.n = s.next.n; s.pending = false;
        std::copy (s.next.slots, s.next.slots + s.n, s.slots);
        std::copy (s.next.clips, s.next.clips + s.n, s.clips);
    }
    s.idx = juce::jmax (0, w.idx);
    s.start = w.start;
    s.passes = w.passes;
    s.cur = step;
    barStep[t] = (int) (step % kBar);
    if (w.idx < 0)
    {
        playingSlot[t] = -1;
        position[t] = -1;
        return;
    }
    const int cs = (int) (step - s.start);
    playingSlot[t] = s.slots[s.idx];
    position[t] = cs;
    if (s.clips[s.idx].columnActive (cs)) hits[t].fetch_add (1, std::memory_order_relaxed);
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
            if (c.a && ! running && ! countingIn)
            {
                if (c.b) { countingIn = true; countFrames = 0; countIn = 4; se_click (1); }
                else startTransport();
            }
            else if (! c.a && (running || countingIn)) stopTransport();
            break;
        case Type::bpm: tempo = c.a; se_set_bpm (c.a); break;
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
        case Type::note: se_note (engineTrack (c.a), c.b, c.c); break;
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
            break;
        }
        case Type::global: se_set_global (c.a, c.b); break;
        case Type::renumber:
            for (int t = 0; t < kTracks; ++t)
            {
                auto& s = tracks[(size_t) t];
                for (int i = 0; i < s.n; ++i) if (s.slots[i] >= c.a) s.slots[i] += c.b;
                for (int i = 0; i < s.next.n; ++i) if (s.next.slots[i] >= c.a) s.next.slots[i] += c.b;
                if (s.n) playingSlot[t] = s.slots[s.idx];
            }
            break;
        case Type::playlist:
        {
            auto& s = tracks[(size_t) c.a];
            if (c.when == When::update)
            {
                for (int i = 0; i < c.n; ++i)
                {
                    for (int j = 0; j < s.n; ++j) if (s.slots[j] == c.slots[i]) s.clips[j] = c.clips[i];
                    if (s.pending)
                        for (int j = 0; j < s.next.n; ++j) if (s.next.slots[j] == c.slots[i]) s.next.clips[j] = c.clips[i];
                }
            }
            else if (c.when == When::nextBar && running)
            {
                s.next = c;
                s.pending = true;
                s.pendingAt = s.cur < 0 ? 0 : (s.cur / kBar + 1) * kBar;
            }
            else
            {
                // at once: from the start of the bar playing (a clip recorded into from here lines up with it)
                s.n = c.n; s.idx = 0; s.passes = 0; s.pending = false;
                s.start = s.cur < 0 ? 0 : s.cur - s.cur % kBar;
                std::copy (c.slots, c.slots + c.n, s.slots);
                std::copy (c.clips, c.clips + c.n, s.clips);
                playingSlot[c.a] = s.n ? s.slots[0] : -1;
            }
            rewrite (c.a);
            break;
        }
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
        if (s.pending)
        {
            s.n = s.next.n; s.pending = false;
            std::copy (s.next.slots, s.next.slots + s.n, s.slots);
            std::copy (s.next.clips, s.next.clips + s.n, s.clips);
        }
        s.idx = 0; s.passes = 0; s.start = 0;
        s.cur = -1; s.lastPos = -1;
        s.seed = rng = mix (rng + 1);
        se_set_locks (engineTrack (t), nullptr, 0);   // (no lock left from the last time anywhere in the loop)
        for (int64_t y = 0; y <= kAhead; ++y) writeStep (t, y);
        s.written = kAhead;
        playingSlot[t] = s.n ? s.slots[0] : -1;
        framesInStep[(size_t) t] = 0;
    }
    se_play (1);
    running = true;
}

void AudioEngine::stopTransport()
{
    se_play (0);
    running = false;
    countingIn = false;
    countIn = 0;
    for (int t = 0; t < kTracks; ++t)
    {
        auto& s = tracks[(size_t) t];
        if (s.pending)
        {
            s.n = s.next.n; s.pending = false;
            std::copy (s.next.slots, s.next.slots + s.n, s.slots);
            std::copy (s.next.clips, s.next.clips + s.n, s.clips);
        }
        s.idx = 0; s.passes = 0; s.start = 0; s.cur = -1; s.lastPos = -1; s.written = -1;
        playingSlot[t] = s.n ? s.slots[0] : -1;
        position[t] = -1;
        barStep[t] = -1;
    }
}

bool AudioEngine::stepPlays (uint32_t seed, int passes, const StepData& x, int lane, int step) const
{
    if (x.prob < 100 && mix (seed ^ (uint32_t) (lane * 73856093) ^ (uint32_t) (step * 19349663)) % 100u >= x.prob)
        return false;
    switch (x.cond)
    {
        case Cond::oneOfTwo: return passes % 2 == 0;
        case Cond::twoOfTwo: return passes % 2 == 1;
        case Cond::oneOfFour: return passes % 4 == 0;
        case Cond::fill: return fill;
        case Cond::notFill: return ! fill;
        case Cond::always: break;
    }
    return true;
}

// the engine's step for `step` (since PLAY): the step of the clip due there, as that pass plays it
void AudioEngine::writeStep (int t, int64_t step)
{
    const auto& s = tracks[(size_t) t];
    const int et = engineTrack (t), e = (int) (step % kMaxSteps);
    const auto w = locate (s, step);
    const ClipData* c = w.idx < 0 ? nullptr : &(w.fromNext ? s.next.clips : s.clips)[w.idx];
    const int cs = c != nullptr ? (int) (step - w.start) : 0;
    const uint32_t seed = mix (s.seed ^ (uint32_t) w.start);
    if (isDrum (t))
    {
        uint16_t mask = 0;
        uint8_t lv[kLanes] {}, rt[kLanes] {};
        int nudge = 0;
        if (c != nullptr)
            for (int lane = 0; lane < kLanes; ++lane)
            {
                const auto& x = c->lanes[(size_t) lane][(size_t) cs];
                if (! x.on || ! stepPlays (seed, w.passes, x, lane, cs)) continue;
                mask = (uint16_t) (mask | 1 << lane);
                lv[lane] = levelOf (x.vel);
                rt[lane] = x.ratchet;
                if (x.nudge != 0) nudge = x.nudge;
            }
        se_drum_step (e, mask, lv, rt, microOf (nudge), SE_FILL_ANY);
    }
    else
    {
        // the notes going on from an earlier step (a long note) first, held; then the ones starting here
        uint8_t notes[4];
        int n = 0;
        unsigned cont = 0;
        const StepData* x = c != nullptr ? &c->notes[(size_t) cs] : nullptr;
        if (c != nullptr)
            for (int r = 0; r < kRows && n < 4; ++r)
                if (const int st = c->noteStart (r, cs); st >= 0 && st < cs && stepPlays (seed, w.passes, c->notes[(size_t) st], 0, st))
                {
                    cont |= 1u << n;
                    notes[n++] = (uint8_t) rowNote (t, r, key);
                }
        const bool starts = x != nullptr && x->rows != 0 && stepPlays (seed, w.passes, *x, 0, cs);
        if (starts)
            for (int r = 0; r < kRows && n < 4; ++r)
                if (x->rows >> r & 1u) notes[n++] = (uint8_t) rowNote (t, r, key);
        se_synth_step_cont (et, e, notes, n, cont, starts ? x->vel : 100, starts && cont == 0 ? x->ratchet : 1,
                            starts ? microOf (x->nudge) : 0, SE_FILL_ANY);
    }
    se_lock_t locks[SE_MAX_LOCKS];
    int count = 0;
    if (c != nullptr)
        for (int i = 0; i < c->nLocks && count < SE_MAX_LOCKS; ++i)
            if (c->locks[(size_t) i].step == cs)
                locks[count++] = { (uint8_t) e, c->locks[(size_t) i].param, c->locks[(size_t) i].value };
    se_set_step_locks (et, e, locks, count);
}
} // namespace sloop
