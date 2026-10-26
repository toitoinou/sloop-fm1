// SPDX-License-Identifier: GPL-3.0-only
// SLOOP Touch --selftest: the audio side without a device (48 kHz, 256-frame blocks, as many phones): the demo
// session plays, a clip waits for the bar, a chain takes turns, the sound is not silent. Exit code 0 = passed.
#include "Controller.h"
#include "DawProject.h"
#include "sloop_engine.h"

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
    double side = 0.0;                                    // (left - right: what is stereo)
    auto run = [&] (double seconds)
    {
        for (int i = 0; i < (int) (seconds * rate / block); ++i)
        {
            audio.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, block, {});
            for (int k = 0; k < block; ++k)
            {
                const double d = (double) out.getSample (0, k) - out.getSample (1, k);
                side += d * d;
            }
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
    // runs until a track's clip is at a step (false: not within two bars)
    auto runTo = [&] (int t, int step)
    {
        for (int i = 0; i < (int) (2.0 * bar * rate / block) + 1; ++i)
        {
            if (audio.position[t].load() == step) return true;
            run (block / rate);
        }
        return false;
    };

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
    ctl.setSound (1, 0, 0);                               // ANALOG: four EDIT pages, 16 parameters
    ctl.setPage (Page::edit4);
    const auto fenv = ctl.knobDefs();
    ctl.setKnob (3, 40);
    check (ctl.pages().size() == 9 && fenv[3].valid && fenv[3].param == SE_P_E0 + 15 && ctl.params[1][(size_t) SE_P_E0 + 15] == 40,
           "ANALOG's fourth EDIT page (the filter envelope) on the knobs");
    ctl.setSound (1, 9, 2);
    check (ctl.page[1] == Page::macro, "an engine without that page: back to its main one");
    ctl.select (0);

    // a long note in the piano roll: held over its steps, not played again on each one
    ctl.launch (1, 0);                                    // T1: one clip again (not the chain above)
    run (bar * 1.2);
    ctl.poll();
    ctl.select (1);
    ctl.editSlot[1] = 0;
    ctl.toggleStep (9, 1, true);                          // row 9, step 2 (empty in the demo's bass lines)
    ctl.setNoteLength (9, 1, 3);
    check (ctl.noteAt (9, 3) == 1 && ctl.noteAt (9, 4) == -1 && ctl.editClip()->data.noteLength (1, 9) == 3,
           "a note stretched over three steps");
    // (an edit of the clip that plays goes in at once; the engine has the few steps ahead of the playhead)
    bool held = runTo (1, 0);
    const int at = se_position (0);
    auto flags = [at] (int k) { return se_step_flags (0, (at + k) % kMaxSteps) & 0x78; };
    check (held && flags (2) != 0 && flags (3) != 0 && flags (1) == 0,
           "its next steps go on from the first (held), not played again");
    ctl.toggleStep (9, 1, false);
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
        ctl.setBpm (118);                                // (the bars below at the first tempo)
        run (0.5);
    }

    ctl.stopTrack (2);
    run (bar * 1.1);
    check (audio.playingSlot[2].load() == -1, "a stopped track stops at the bar");

    // clips of any length: a short one loops on its own steps, a long one launched starts on the bar
    {
        ctl.setLength (3, 0, 6);                         // T3: Motif, 6 steps
        ctl.launch (3, 0);
        run (bar * 1.05);
        int most = -1;
        for (int i = 0; i < 40; ++i) { run (0.05); most = juce::jmax (most, audio.position[3].load()); }
        check (most == 5, "a 6-step clip loops on its 6 steps");
        ctl.setLength (3, 0, 32);                        // 2 bars, launched again: from the next bar
        ctl.doubleClip (3, 1);
        ctl.launch (3, 0);
        run (bar);
        const bool onBar = runTo (3, 16) && audio.position[0].load() % kBar == 0;
        check (onBar && runTo (3, 31), "a 32-step clip plays its two bars, on the bar");
        check (ctl.session.at (3, 1)->data.length == 32 && ctl.session.at (3, 1)->data.notes[18].rows == ctl.session.at (3, 1)->data.notes[2].rows,
               "doubling a clip plays it twice");
    }

    // undo / redo, copy / paste
    {
        ctl.select (0);
        ctl.editSlot[0] = 0;
        const bool was = ctl.editClip()->data.lanes[9][3].on != 0;
        juce::Thread::sleep (450);                       // (a new edit: not merged with the ones before)
        ctl.toggleStep (9, 3, ! was);
        ctl.undo();
        const bool undone = (ctl.editClip()->data.lanes[9][3].on != 0) == was;
        ctl.redo();
        check (undone && (ctl.editClip()->data.lanes[9][3].on != 0) != was, "undo and redo a step");
        const int scenes = ctl.session.scenes();
        ctl.copyClip (0, 0);
        ctl.pasteClip (0, scenes - 1);
        bool same = ctl.session.at (0, scenes - 1).has_value();
        for (int i = 0; same && i < kMaxSteps; ++i)
            same = ctl.session.at (0, scenes - 1)->data.lanes[0][(size_t) i].on == ctl.session.at (0, 0)->data.lanes[0][(size_t) i].on;
        check (same && ! ctl.canPaste (1),
               "a clip copied and pasted (a drum clip on the drum track only)");
        juce::Thread::sleep (450);
        ctl.deleteScene (scenes - 1);
        ctl.undo();
        check (ctl.session.scenes() == scenes && ctl.session.at (0, scenes - 1), "undo a deleted scene");
    }

    // the key: the rows are its degrees
    {
        ctl.setKey ({ 2, 1 });                           // D major
        check (ctl.note (1, 0) == 36 + 2 && ctl.note (1, 2) == 36 + 2 + 4 && ctl.note (1, 7) == 48 + 2, "the piano roll follows the key");
        ctl.setKey ({});
    }

    // recording between the steps (quantize off): a nudge
    {
        ctl.prefs.quantizeRec = false;
        ctl.toggleRecord();
        ctl.select (0);
        runTo (0, 6);
        run (0.06);                                      // (a third or so into the step)
        const int slot = audio.playingSlot[0].load();
        ctl.recordHit (13, 90);
        auto& x = ctl.session.at (0, slot)->data.lanes[13];
        int nudged = 0;
        for (auto& st : x) if (st.on && st.nudge != 0) ++nudged;
        check (nudged == 1, "a hit recorded off the grid keeps its place (a nudge)");
        ctl.toggleRecord();
        ctl.prefs.quantizeRec = true;
    }

    // a key held while recording: the note lasts as long as it was held
    {
        ctl.select (1);
        ctl.toggleRecord();
        runTo (1, 1);
        const int slot = audio.playingSlot[1].load();
        ctl.recordHit (20, 100);                         // (row 20: empty in the demo's lines)
        const double stepSec = 15.0 / ctl.bpm;
        run (stepSec * 3.0);
        ctl.recordRelease (1, 20);
        auto& d = ctl.session.at (1, slot)->data;
        int start = -1;
        for (int st = 0; st < d.length; ++st) if (d.notes[(size_t) st].rows >> 20 & 1u) start = st;
        check (start >= 0 && d.noteLength (start, 20) >= 3 && d.noteLength (start, 20) <= 4,
               "a key held while recording: the note keeps its length");
        // a knob turned while recording: the move is written on the steps it passes
        ctl.setPage (Page::mix);
        int written = 0;
        for (int i = 0; i < 4; ++i)
        {
            runTo (1, 4 + i);
            ctl.setKnob (1, 20 + i * 10);                 // FILTRE (the track's filter)
        }
        for (int st = 4; st < 8; ++st) written += d.lock (st, SE_P_TFLT) != kNoLock;
        check (written == 4, "a knob turned while recording: its moves on the steps");
        ctl.clearMotion (1, slot);
        ctl.toggleRecord();
        ctl.select (0);
    }

    // note repeat held while recording: a hit on every step it was held (1/16)
    {
        ctl.select (0);
        ctl.toggleRecord();
        const int slot = audio.playingSlot[0].load();
        runTo (0, 2);
        ctl.repeatRate = 2;
        ctl.repeatPress (0, 15, 15, 90);                 // (lane 15, cowbell: empty in the demo)
        for (int i = 0; i < 40; ++i) { run (0.02); ctl.poll(); }   // (about six steps)
        ctl.repeatRelease (0, 15);
        int hits = 0;
        for (auto& x : ctl.session.at (0, slot)->data.lanes[15]) hits += x.on;
        check (hits >= 4 && hits <= 7, "note repeat while recording: a hit on each step held");
        ctl.toggleRecord();
    }

    // REC from stopped: a bar of clicks, then the song
    {
        ctl.togglePlay();
        run (0.1);
        ctl.prefs.countIn = true;
        ctl.toggleRecord();
        run (0.1);
        const bool counting = audio.countIn.load() > 0 && ! audio.running.load();
        run (60.0 / ctl.bpm * 4.0);                      // (the tempo of the project loaded above)
        check (counting && audio.running.load() && audio.countIn.load() == 0, "a count-in, then the song plays");
        ctl.toggleRecord();
    }

    // the DAWproject file: the tracks, the scenes, the notes, in a zip
    {
        const auto snap = ctl.snapshot();
        const auto xml = juce::XmlDocument::parse (dawproject::projectXml (snap));
        int scenes = 0, notes = 0, tracks = 0;
        if (xml != nullptr)
        {
            if (auto* sc = xml->getChildByName ("Scenes")) scenes = sc->getNumChildElements();
            if (auto* st = xml->getChildByName ("Structure")) tracks = st->getNumChildElements();
            std::function<void (const juce::XmlElement&)> count = [&] (const juce::XmlElement& e)
            {
                if (e.hasTagName ("Note")) ++notes;
                for (auto* c : e.getChildIterator()) count (*c);
            };
            count (*xml);
        }
        const auto data = dawproject::zip (snap);
        juce::MemoryInputStream in (data, false);
        juce::ZipFile zip (in);
        check (xml != nullptr && scenes == snap.session.scenes() && tracks == kTracks + 1 && notes > 100
                   && zip.getNumEntries() == 2 && zip.getEntry ("project.xml") != nullptr,
               "a DAWproject export (tracks, scenes, notes)");
    }

    check (std::sqrt (energy / (double) frames) > 0.005, "the engine makes sound");
    ctl.togglePlay();
    run (0.1);
    check (! audio.running.load(), "the transport stops");

    // PLAITS (Mutable Instruments' Plaits): every model sounds from the keyboard (the song stopped)
    {
        int plaits = 0;
        while (plaits < se_engine_count() && juce::String (se_engine_name (plaits)) != "PLAITS") ++plaits;
        int silent = 0;
        ctl.setSound (2, plaits, 0);                     // T2: VA LEAD, then each model
        for (int model = 0; model < 24; ++model)
        {
            ctl.audio.setParam (2, SE_P_E0, model);
            double mark = energy;
            run (0.25);
            const double quiet = energy - mark;          // (what still rings from before)
            mark = energy;
            ctl.audio.note (2, 60, true);
            run (0.25);
            ctl.audio.note (2, 60, false);
            if (energy - mark < quiet * 2.0 + 1.0) { ++silent; std::printf ("      model %d: %.2f / %.2f\n", model, energy - mark, quiet); }
            run (0.5);
        }
        check (juce::String (se_engine_name (plaits)) == "PLAITS" && silent == 0, "PLAITS: its 24 models all sound");
        ctl.setSound (2, 1, 0);
    }

    // the mixer: solo leaves only the track soloed, the master volume scales the output
    {
        ctl.setSound (2, 0, 13);
        ctl.audio.note (2, 60, true);
        run (0.3);
        audio.peak[2].exchange (0);
        audio.peak[1].exchange (0);
        ctl.toggleSolo (1);                              // T1 soloed (silent now): T2 is not heard
        run (0.1);                                       // (its fade out)
        audio.peak[2].exchange (0);
        run (0.3);
        const int soloed = audio.peak[2].exchange (0);
        ctl.toggleSolo (1);
        ctl.audio.note (2, 60, false);                   // (a note played again: heard)
        ctl.audio.note (2, 60, true);
        run (0.6);
        const int back = audio.peak[2].exchange (0);
        ctl.audio.note (2, 60, false);
        if (! (soloed == 0 && back > 0)) std::printf ("      soloed %d back %d\n", soloed, back);
        check (soloed == 0 && back > 0, "the mixer: a solo silences the other tracks");
        run (0.5);
    }

    // recording the output: a WAV as long as the time recorded
    {
        const auto file = juce::File::createTempFile (".wav");
        const bool started = audio.startRecording (file);
        ctl.audio.note (2, 60, true);
        for (int i = 0; i < 5; ++i) { run (0.2); juce::Thread::sleep (30); }   // (faster than real time: the writer keeps up)
        ctl.audio.note (2, 60, false);
        audio.stopRecording();
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        const double seconds = reader != nullptr ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
        check (started && reader != nullptr && reader->numChannels == 2 && seconds > 0.9 && seconds < 1.1,
               "recording the output: a WAV as long as the time recorded");
        reader.reset();
        file.deleteFile();
    }

    // MIDI in: a note plays the track selected (from the audio thread), CC 70 turns its first knob
    {
        ctl.select (2);
        ctl.setSound (2, 0, 2);
        run (0.3);
        audio.peak[2].exchange (0);
        auto on = juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110);
        on.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        audio.midiIn.addMessageToQueue (on);
        run (0.3);
        const int heard = audio.peak[2].exchange (0);
        auto off = juce::MidiMessage::noteOff (1, 48);
        off.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        audio.midiIn.addMessageToQueue (off);
        ctl.setPage (Page::mix);
        ctl.midiCC (70, 0);
        const int level = ctl.params[2][(size_t) SE_P_LEVEL];
        check (heard > 0 && level == 0, "MIDI in: a note plays the track selected, a CC turns a knob");
        ctl.midiCC (70, 104);
        ctl.select (0);
        run (0.5);
        audio.peak[2].exchange (0);
        run (0.5);
        const int afterOff = audio.peak[2].exchange (0);
        check (afterOff == 0, "MIDI in: its note off ends the note (the track selected since changed)");
    }

    // OBXD: every preset sounds
    {
        const int obxd = se_engine_count() - 1;
        int silent = 0;
        for (int p = 0; p < se_preset_count (obxd); ++p)
        {
            ctl.setSound (2, obxd, p);
            run (0.3);
            double mark = energy;
            run (0.2);
            const double quiet = energy - mark;
            mark = energy;
            ctl.audio.note (2, 60, true);
            run (0.3);
            ctl.audio.note (2, 60, false);
            if (energy - mark < quiet * 2.0 + 1.0) { ++silent; std::printf ("      %s: %.2f / %.2f\n", se_preset_name (obxd, p), energy - mark, quiet); }
            run (0.6);
        }
        check (juce::String (se_engine_name (obxd)) == "OBXD" && silent == 0, "OBXD: its presets all sound");
    }

    // the MOD stage: a Juno chorus is stereo, a vibrato sounds
    {
        ctl.setSound (2, 0, 13);                         // ANALOG WARM PAD, centred
        ctl.audio.setParam (2, SE_P_PAN, 0);
        for (int send : { (int) SE_P_CHOR, (int) SE_P_DLY, (int) SE_P_REV }) ctl.audio.setParam (2, send, 0);   // (the sends are stereo)
        ctl.audio.setParam (2, SE_P_MTYPE, 0);
        run (2.0);
        double mark = side;
        ctl.audio.note (2, 60, true);
        run (0.5);
        const double dry = side - mark;
        ctl.audio.setParam (2, SE_P_MTYPE, 1);           // JUNO I
        run (0.1);
        mark = side;
        run (0.5);
        const double juno = side - mark;
        ctl.audio.setParam (2, SE_P_MTYPE, 4);           // VIBRATO
        run (0.1);
        mark = energy;
        run (0.5);
        const double vib = energy - mark;
        ctl.audio.note (2, 60, false);
        ctl.audio.setParam (2, SE_P_MTYPE, 0);
        run (0.5);
        check (juno > dry * 10.0 + 1e-3 && vib > 1.0, "MOD: the Juno chorus is stereo, the vibrato sounds");
    }
    // Ableton Link, alone on the network (in real time: Link's clock is the system's): PLAY starts at once, the song
    // follows the session's tempo (another app's) and is on its beat; the session's STOP stops it
    {
        auto runRT = [&] (double seconds)
        {
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < (int) (seconds * rate / block); ++i)
            {
                run (block / rate);
                const double due = t0 + (i + 1) * block / rate * 1000.0;
                while (juce::Time::getMillisecondCounterHiRes() < due) juce::Thread::sleep (1);
            }
        };
        if (ctl.playing) ctl.togglePlay();
        ctl.prefs.link = true;                           // (not setPrefs: no prefs file written)
        audio.setLink (true);
        runRT (0.2);
        ctl.setBpm (100);
        ctl.togglePlay();
        runRT (0.3);
        ctl.poll();
        const bool started = audio.running.load() && ctl.playing && std::abs (audio.linkTempo.load() - 100.0) < 0.01;
        audio.link.setTempo (140.0, audio.link.now());   // (as another app would: the test runs the audio thread)
        runRT (1.5);
        ctl.poll();
        const int followed = ctl.bpm;
        const auto st = audio.link.capture (audio.link.now(), audio.link.now());
        const double expected = std::fmod (st.beatStart, 4.0) * 4.0, stepAt = audio.barStep[0].load();
        const double off = std::abs (std::remainder (stepAt - expected, 16.0));
        std::printf ("      Link: started %d, %d BPM, step %.0f, the session's %.2f\n", (int) started, followed, stepAt, expected);
        audio.link.setPlaying (false, audio.link.now());
        runRT (0.2);
        ctl.poll();
        const bool stopped = ! audio.running.load() && ! ctl.playing;
        ctl.prefs.link = false;
        audio.setLink (false);
        ctl.setBpm (118);
        run (0.1);
        check (started && followed == 140 && off <= 1.5 && stopped,
               "Link: PLAY, the session's tempo and beat, the session's STOP");
    }
    std::printf ("%s\n", failures == 0 ? "self-test passed" : "self-test FAILED");
    return failures == 0 ? 0 : 1;
}
} // namespace sloop
