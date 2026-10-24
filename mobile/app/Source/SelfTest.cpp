// SPDX-License-Identifier: GPL-3.0-only
// SLOOP Touch --selftest: the audio side without a device (48 kHz, 256-frame blocks, as many phones): the demo
// session plays, a clip waits for the bar, a chain takes turns, the sound is not silent. Exit code 0 = passed.
#include "Controller.h"

namespace sloop
{
int runSelfTest()
{
    AudioEngine audio;
    Controller ctl (audio, false);
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

    const int serial = audio.paramSerial.load();
    ctl.setSound (1, 9, 2);                               // T1: FM6 · ROUND BASS
    ctl.setSound (0, -1, 6);                              // DR: the 909 kit
    run (0.2);
    ctl.poll();
    check (audio.paramSerial.load() > serial && ctl.seenParamSerial == audio.paramSerial.load(),
           "a new sound reports its parameters back");

    ctl.select (1);                                       // T1's own parameters, page by page
    ctl.setPage (Page::edit1);
    const auto defs = ctl.knobDefs();
    check (defs[0].valid && defs[0].param == SE_P_E0, "the engine's EDIT parameters are on the knobs");
    ctl.setKnob (0, defs[0].min + 3);
    check (ctl.params[1][(size_t) SE_P_E0] == defs[0].min + 3, "a knob sets its parameter");
    ctl.held = { { 0, 4 } };                              // step 5 held: a lock on an engine parameter
    ctl.setKnob (0, defs[0].min + 5);
    check (ctl.editClip()->data.lock (4, SE_P_E0) == defs[0].min + 5, "a held step locks any parameter");
    ctl.held.clear();
    ctl.select (0);

    ctl.toggleRecord();                                   // REC: a pad played goes into the playing clip
    auto& rec = ctl.session.at (0, audio.playingSlot[0].load())->data;
    int before = 0;
    for (auto& x : rec.lanes[9]) before += x.on;
    ctl.recordHit (9, 100);                               // lane 9 (low tom): empty in the demo
    run (0.3);
    ctl.poll();
    int after = 0;
    for (auto& x : rec.lanes[9]) after += x.on;
    check (after == before + 1, "a pad played while recording writes a step");
    ctl.toggleRecord();

    // a project: saved as JSON, read back, loaded
    {
        ctl.setBpm (131);
        auto saved = ctl.snapshot();
        saved.name = "Test";
        const auto back = Project::fromJson (saved.toJson());
        check (back.has_value() && back->bpm == 131 && back->sounds[1].engine == 9 && back->sounds[1].preset == 2
                   && back->session.scenes() == ctl.session.scenes()
                   && back->session.at (0, 0)->data.lanes[0][0].on == 1
                   && back->session.at (1, 0)->data.notes[0].rows == ctl.session.at (1, 0)->data.notes[0].rows
                   && back->params[1][(size_t) SE_P_E0] == ctl.params[1][(size_t) SE_P_E0]
                   && back->session.at (1, 0)->data.lock (4, SE_P_E0) == ctl.session.at (1, 0)->data.lock (4, SE_P_E0),
               "a project survives its file");
        auto other = *back;
        other.bpm = 97;
        other.params[1][(size_t) SE_P_E0] = defs[0].min + 1;
        const bool wasPlaying = ctl.playing;
        ctl.load (other);
        run (0.2);
        ctl.poll();
        check (ctl.bpm == 97 && ! ctl.playing && ctl.params[1][(size_t) SE_P_E0] == defs[0].min + 1,
               "loading a project sets the tempo, the sounds and their parameters");
        if (wasPlaying) ctl.togglePlay();
        run (0.5);
    }

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
