// SPDX-License-Identifier: GPL-3.0-only
#include "AudioEngine.h"
#include "sloop_engine.h"

namespace sloop
{
namespace
{
uint8_t levelOf (int vel) { return vel < 50 ? SE_LV_GHOST : vel < 80 ? SE_LV_SOFT : vel < 112 ? SE_LV_NORM : SE_LV_HARD; }
int microOf (int sixths) { return juce::jlimit (-32, 31, sixths * 64 / 6); }   // the engine's nudge: 1/64 of a step
uint32_t mix (uint32_t h)
{
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
} // namespace

AudioEngine::AudioEngine()
{
    queue.resize ((size_t) fifo.getTotalSize());
    for (int t = 0; t < kTracks; ++t) { playingSlot[t] = -1; position[t] = -1; hits[t] = 0; stepPhase[t] = 0.0f; }
    se_init();
    se_set_bpm (tempo);
    se_set_kit (kDefaultKit);
    for (int t = 1; t < kTracks; ++t)
        se_set_sound (engineTrack (t), kDefaultSounds[(size_t) t].engine, kDefaultSounds[(size_t) t].preset);
    for (int t = 0; t < kTracks; ++t) { se_set_length (engineTrack (t), kSteps); publishParams (t); }
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
void AudioEngine::play (bool on) { Command c; c.type = Type::play; c.a = on; post (std::move (c)); }
void AudioEngine::setBpm (int bpm) { Command c; c.type = Type::bpm; c.a = bpm; post (std::move (c)); }
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
    for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
        if (out[ch] != nullptr)
            used = interp[ch].process (ratio, staging.getReadPointer (ch), out[ch], numSamples);
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
    if (! running.load (std::memory_order_relaxed)) return;

    const float stepFrames = (float) se_sample_rate() * 15.0f / (float) tempo;   // a 1/16 step
    for (int t = 0; t < kTracks; ++t)
    {
        auto& s = tracks[(size_t) t];
        const int pos = se_position (engineTrack (t));
        framesInStep[(size_t) t] = pos == s.lastPos ? framesInStep[(size_t) t] + frames : 0;
        stepPhase[t].store (juce::jlimit (0.0f, 1.0f, (float) framesInStep[(size_t) t] / stepFrames), std::memory_order_relaxed);
        if (pos == s.lastPos) continue;
        const int len = s.n ? s.clips[s.idx].length : kSteps;
        if (pos == len - 1)
            prepareNextLoop (t);                     // the next loop goes in while its last step plays
        else if (pos == 0 && s.lastStepPending)
        {
            writeSteps (t, len - 1, -1);
            s.lastStepPending = false;
            playingSlot[t] = s.reportSlot;
        }
        if (pos >= 0 && s.n && s.clips[s.idx].columnActive (pos)) hits[t].fetch_add (1, std::memory_order_relaxed);
        s.lastPos = pos;
        position[t] = pos;
    }
}

void AudioEngine::apply (Command& c)
{
    switch (c.type)
    {
        case Type::play:
            if (c.a && ! running) startTransport();
            else if (! c.a && running)
            {
                se_play (0);
                running = false;
                for (int t = 0; t < kTracks; ++t)
                {
                    auto& s = tracks[(size_t) t];
                    if (s.pending)
                    {
                        s.n = s.next.n; s.idx = 0; s.pending = false;
                        std::copy (s.next.slots, s.next.slots + s.n, s.slots);
                        std::copy (s.next.clips, s.next.clips + s.n, s.clips);
                    }
                    s.idx = 0; s.lastPos = -1; s.lastStepPending = false;
                    playingSlot[t] = s.n ? s.slots[0] : -1;
                    position[t] = -1;
                }
            }
            break;
        case Type::bpm: tempo = c.a; se_set_bpm (c.a); break;
        case Type::fill:
            fill = c.a != 0;
            se_set_fill (0);                         // the app resolves the fill conditions itself (per drum sound)
            if (running)
                for (int t = 0; t < kTracks; ++t)
                    writeSteps (t, -1, tracks[(size_t) t].lastStepPending ? kSteps - 1 : -1);
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
                if (running) writeSteps (c.a, -1, s.lastStepPending ? kSteps - 1 : -1);
            }
            else if (c.when == When::nextBar && running)
            {
                s.next = c;
                s.pending = true;
            }
            else
            {
                s.n = c.n; s.idx = 0; s.loops = 0; s.pending = false;
                std::copy (c.slots, c.slots + c.n, s.slots);
                std::copy (c.clips, c.clips + c.n, s.clips);
                playingSlot[c.a] = s.n ? s.slots[0] : -1;
                if (running) writeSteps (c.a, -1, -1);
            }
            break;
        }
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
        s.idx = 0; s.loops = 0; s.seed = rng = mix (rng + 1);
        s.lastPos = -1; s.lastStepPending = false;
        writeSteps (t, -1, -1);
        playingSlot[t] = s.n ? s.slots[0] : -1;
    }
    se_play (1);
    running = true;
}

void AudioEngine::prepareNextLoop (int t)
{
    auto& s = tracks[(size_t) t];
    const int prev = s.n ? s.slots[s.idx] : -1;
    if (s.pending)
    {
        s.n = s.next.n; s.idx = 0; s.pending = false;
        std::copy (s.next.slots, s.next.slots + s.n, s.slots);
        std::copy (s.next.clips, s.next.clips + s.n, s.clips);
    }
    else if (s.n > 1)
        s.idx = (s.idx + 1) % s.n;
    const int now = s.n ? s.slots[s.idx] : -1;
    s.loops = now == prev ? s.loops + 1 : 0;
    s.seed = rng = mix (rng + 1);
    s.reportSlot = now;
    writeSteps (t, -1, kSteps - 1);
    s.lastStepPending = true;
}

bool AudioEngine::stepPlays (const TrackState& s, const StepData& x, int lane, int step) const
{
    if (x.prob < 100 && mix (s.seed ^ (uint32_t) (lane * 73856093) ^ (uint32_t) (step * 19349663)) % 100u >= x.prob)
        return false;
    switch (x.cond)
    {
        case Cond::oneOfTwo: return s.loops % 2 == 0;
        case Cond::twoOfTwo: return s.loops % 2 == 1;
        case Cond::oneOfFour: return s.loops % 4 == 0;
        case Cond::fill: return fill;
        case Cond::notFill: return ! fill;
        case Cond::always: break;
    }
    return true;
}

void AudioEngine::writeSteps (int t, int onlyStep, int skipStep)
{
    auto& s = tracks[(size_t) t];
    const int et = engineTrack (t);
    const ClipData* c = s.n ? &s.clips[s.idx] : nullptr;
    for (int step = 0; step < kSteps; ++step)
    {
        if ((onlyStep >= 0 && step != onlyStep) || step == skipStep) continue;
        if (isDrum (t))
        {
            uint16_t mask = 0;
            uint8_t lv[kLanes] {}, rt[kLanes] {};
            int nudge = 0;
            if (c != nullptr)
                for (int lane = 0; lane < kLanes; ++lane)
                {
                    const auto& x = c->lanes[(size_t) lane][(size_t) step];
                    if (! x.on || ! stepPlays (s, x, lane, step)) continue;
                    mask = (uint16_t) (mask | 1 << lane);
                    lv[lane] = levelOf (x.vel);
                    rt[lane] = x.ratchet;
                    if (x.nudge != 0) nudge = x.nudge;
                }
            se_drum_step (step, mask, lv, rt, microOf (nudge), SE_FILL_ANY);
        }
        else
        {
            uint8_t notes[4];
            int n = 0;
            const StepData* x = c != nullptr ? &c->notes[(size_t) step] : nullptr;
            if (x != nullptr && x->rows != 0 && stepPlays (s, *x, 0, step))
                for (int r = 0; r < kRows && n < 4; ++r)
                    if (x->rows >> r & 1) notes[n++] = (uint8_t) rowNote (t, r);
            se_synth_step (et, step, notes, n, x != nullptr ? x->vel : 100, x != nullptr ? x->ratchet : 1,
                           x != nullptr ? microOf (x->nudge) : 0, SE_FILL_ANY);
        }
    }
    if (onlyStep >= 0) return;
    se_lock_t locks[SE_MAX_LOCKS];
    int count = 0;
    if (c != nullptr)
        for (int i = 0; i < c->nLocks && count < SE_MAX_LOCKS; ++i)
            locks[count++] = { c->locks[(size_t) i].step, c->locks[(size_t) i].param, c->locks[(size_t) i].value };
    se_set_locks (et, locks, count);
}
} // namespace sloop
