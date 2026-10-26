// SPDX-License-Identifier: GPL-3.0-only
// The audio track's player, on the audio thread: an audio clip on the song's beats. Its loop (the sample from its
// start to its end) lasts its beats; the song's beat says where in it the player is, block by block, so it never
// drifts from the tracks (the tempo moves, Link moves it: it follows). Stretched (Signalsmith Stretch: the pitch
// kept) or read faster or slower (like a record: the pitch follows). No lock, no allocation once made.
#pragma once
#include "Samples.h"

namespace sloop
{
class ClipPlayer
{
public:
    ClipPlayer();
    ~ClipPlayer();

    struct Clip
    {
        const SampleData* sample = nullptr;              // (kept alive by the app's SampleBank)
        int64_t start = 0, end = 0;
        int beats = 16;
        bool stretch = true;
        float gain = 1.0f;
    };
    void start (const Clip&, double atBeat);             // its first beat on the song's beat `atBeat`
    void update (const Clip&);                           // the same clip edited (where it is in its loop kept)
    void stop() { on = false; }
    bool playing() const { return on; }
    double progress (double beat) const;                 // 0..1 in its loop
    double beatsPerFrame() const { return clip.end > clip.start ? clip.beats / (double) (clip.end - clip.start) : 0.0; }   // its own pace
    // adds the song from beat b0 to b1, `frames` frames at the engine's rate (nothing before its first beat)
    void render (double b0, double b1, float* l, float* r, int frames);

private:
    double framesPerBeat() const { return (double) (clip.end - clip.start) / clip.beats; }
    float read (int ch, double v) const;                 // the loop at virtual frame v (0: its start; looped)
    void renderStretched (double b0, double b1, float* l, float* r, int frames);

    Clip clip;
    double origin = 0;
    bool on = false, primed = false;
    double fed = 0;                                      // the loop's virtual frames given to the stretcher so far
    struct Stretch;
    std::unique_ptr<Stretch> stretcher;
};
} // namespace sloop
