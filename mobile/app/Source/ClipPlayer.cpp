// SPDX-License-Identifier: GPL-3.0-only
#include "ClipPlayer.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace sloop
{
namespace
{
constexpr int kFade = 96;                                // the loop's ends faded (2 ms): no click where it joins
constexpr int kScratch = 1 << 15;                        // the most input the stretcher gets at once (a seek)
double wrap (double v, double len) { const double m = std::fmod (v, len); return m < 0 ? m + len : m; }
} // namespace

struct ClipPlayer::Stretch
{
    signalsmith::stretch::SignalsmithStretch<float> s;
    std::vector<float> in[2], out[2];
    Stretch()
    {
        s.presetDefault (2, (float) AudioClip::rate, true);   // (split: its work spread over the blocks)
        for (auto& b : in) b.resize (kScratch);
        for (auto& b : out) b.resize (4096);
    }
};

ClipPlayer::ClipPlayer() : stretcher (std::make_unique<Stretch>()) {}
ClipPlayer::~ClipPlayer() = default;

void ClipPlayer::start (const Clip& c, double atBeat)
{
    clip = c;
    on = c.sample != nullptr && c.end > c.start && c.beats > 0;
    origin = atBeat;
    primed = false;
}

void ClipPlayer::update (const Clip& c)
{
    const bool restretch = c.stretch != clip.stretch || c.start != clip.start || c.end != clip.end || c.beats != clip.beats;
    clip = c;
    on = on && c.sample != nullptr && c.end > c.start && c.beats > 0;
    if (restretch) primed = false;
}

double ClipPlayer::progress (double beat) const
{
    return on && beat >= origin ? wrap (beat - origin, clip.beats) / clip.beats : 0.0;
}

float ClipPlayer::read (int ch, double v) const
{
    const auto len = (double) (clip.end - clip.start);
    const double x = wrap (v, len);
    const auto i = (int64_t) x;
    const float f = (float) (x - (double) i);
    const auto* d = clip.sample->buffer.getReadPointer (ch);
    const int64_t n = clip.sample->buffer.getNumSamples();
    auto at = [&] (int64_t k)                            // (looped within the clip: its end is followed by its start)
    {
        const int64_t m = clip.end - clip.start;
        k = ((k % m) + m) % m;
        return d[juce::jlimit ((int64_t) 0, n - 1, clip.start + k)];
    };
    // 4-point Hermite
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    const float y = ((c3 * f + c2) * f + c1) * f + y1;
    const double edge = juce::jmin (x, len - x);
    return edge < kFade ? y * (float) (edge / kFade) : y;
}

void ClipPlayer::render (double b0, double b1, float* l, float* r, int frames)
{
    if (! on || frames <= 0 || b1 <= b0 || b1 <= origin) return;
    const double perFrame = (b1 - b0) / frames;
    if (origin > b0)                                     // its first beat falls in the block: from there on
    {
        const int skip = juce::jlimit (0, frames, (int) std::ceil ((origin - b0) / perFrame));
        l += skip; r += skip; frames -= skip;
        b0 += skip * perFrame;
        if (frames <= 0) return;
    }
    if (clip.stretch) { renderStretched (b0, b1, l, r, frames); return; }
    const double fpb = framesPerBeat();
    for (int i = 0; i < frames; ++i)
    {
        const double v = (b0 + i * perFrame - origin) * fpb;
        l[i] += read (0, v) * clip.gain;
        r[i] += read (1, v) * clip.gain;
    }
}

// the stretcher's input runs ahead of what it gives out: fed so that its output at beat b is the loop's frame of b
// (its input latency, and its output latency at the block's rate, ahead)
void ClipPlayer::renderStretched (double b0, double b1, float* l, float* r, int frames)
{
    auto& st = *stretcher;
    const double fpb = framesPerBeat(), rate = (b1 - b0) / frames * fpb;   // the loop's frames per frame out
    auto feed = [&] (double from, int count)
    {
        count = juce::jlimit (0, kScratch, count);
        for (int i = 0; i < count; ++i)
        {
            st.in[0][(size_t) i] = read (0, from + i);
            st.in[1][(size_t) i] = read (1, from + i);
        }
        return count;
    };
    float* ins[2] = { st.in[0].data(), st.in[1].data() };
    if (! primed)
    {
        const double at = (b0 - origin) * fpb;
        const int n = feed (at, st.s.outputSeekLength ((float) rate));
        st.s.outputSeek (ins, n);
        fed = at + n;
        primed = true;
    }
    const double target = (b1 - origin) * fpb + st.s.inputLatency() + rate * st.s.outputLatency();
    int count = (int) std::llround (target - fed);
    if (count < 0 || count > 8 * frames * juce::jmax (1.0, rate) + 64)   // lost (a jump): from where it is
    {
        primed = false;
        renderStretched (b0, b1, l, r, frames);
        return;
    }
    count = feed (fed, count);
    fed += count;
    for (int done = 0; done < frames;)
    {
        const int n = juce::jmin (frames - done, (int) st.out[0].size());
        float* outs[2] = { st.out[0].data(), st.out[1].data() };
        st.s.process (ins, done == 0 ? count : 0, outs, n);
        for (int i = 0; i < n; ++i)
        {
            l[done + i] += st.out[0][(size_t) i] * clip.gain;
            r[done + i] += st.out[1][(size_t) i] * clip.gain;
        }
        done += n;
    }
}
} // namespace sloop
