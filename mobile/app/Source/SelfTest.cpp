// SPDX-License-Identifier: GPL-3.0-only
// SLOOP Touch --selftest: the audio side without a device (48 kHz, 256-frame blocks, as many phones): the demo
// session plays, a clip waits for the bar, a chain takes turns, the sound is not silent. Exit code 0 = passed.
#include "Controller.h"

namespace sloop
{
int runSelfTest()
{
    AudioEngine audio;
    Controller ctl (audio);
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    audio.prepare (rate, block);
    juce::AudioBuffer<float> out (2, block);
    double energy = 0.0;
    long frames = 0;
    auto run = [&] (double seconds)
    {
        for (int i = 0; i < (int) (seconds * rate / block); ++i)
        {
            audio.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, block, {});
            for (int k = 0; k < block; ++k) energy += (double) out.getSample (0, k) * out.getSample (0, k);
            frames += block;
        }
    };
    int failures = 0;
    auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    };
    const double bar = 60.0 / ctl.bpm * 4.0;

    ctl.togglePlay();
    run (0.5);
    check (audio.running.load(), "the transport runs");
    check (audio.playingSlot[0].load() == 0 && audio.playingSlot[1].load() == 0, "scene 1 plays");
    check (audio.position[0].load() >= 0, "the drum track moves");

    ctl.launch (0, 1);                                    // DR: Groove + shaker, at the next bar
    run (0.1);
    check (audio.playingSlot[0].load() == 0, "a launched clip waits for the bar");
    run (bar);
    ctl.poll();
    check (audio.playingSlot[0].load() == 1, "it plays from the next bar");
    check (ctl.queued[0] == Controller::kNone, "no longer shown as waiting");

    ctl.chain (1, { 0, 1 });                              // T1: Ligne, Ligne + in turn
    run (bar * 1.2);
    const int first = audio.playingSlot[1].load();
    run (bar);
    const int second = audio.playingSlot[1].load();
    check (first != second && (first == 0 || first == 1) && (second == 0 || second == 1), "a chain takes turns");

    ctl.stopTrack (2);
    run (bar * 1.1);
    check (audio.playingSlot[2].load() == -1, "a stopped track stops at the bar");

    check (std::sqrt (energy / (double) frames) > 0.005, "the engine makes sound");
    ctl.togglePlay();
    run (0.1);
    check (! audio.running.load(), "the transport stops");
    std::printf ("%s\n", failures == 0 ? "self-test passed" : "self-test FAILED");
    return failures == 0 ? 0 : 1;
}
} // namespace sloop
