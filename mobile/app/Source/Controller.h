// SPDX-License-Identifier: GPL-3.0-only
// What the screens share: the session, the selection, the clips waiting for the bar, the steps held for a
// parameter lock. It turns the screens' gestures into the audio engine's commands.
#pragma once
#include "AudioEngine.h"
#include "Project.h"
#include "sloop_engine.h"
#include <functional>

namespace sloop
{
class Controller
{
public:
    // restore: reopen the autosave of the last time (else the demo); the self-test starts from the demo
    explicit Controller (AudioEngine& a, bool restore = true) : audio (a)
    {
        readParams();
        if (restore) prefs = store::readPrefs();
        audio.setMetronome (prefs.metronome);
        if (auto saved = restore ? store::read (store::autosaveFile()) : std::nullopt) load (*saved);
        else
        {
            Project demo;
            demo.name = juce::String::fromUTF8 ("Démo");
            demo.session = demoSession();
            load (demo);
        }
    }

    AudioEngine& audio;
    Session session;
    juce::String projectName;
    int sel = 0;                                         // the selected track
    std::array<int, kTracks> editSlot {};                // the clip each track edits
    int lane = 0;                                        // the drum sound the knobs and pads point at
    std::array<int, kTracks> queued { { kNone, kNone, kNone, kNone } };   // a row waiting for the bar, kStop, kNone
    std::array<std::vector<int>, kTracks> playlist;      // the rows each track plays in turn (a chain)
    std::array<bool, kTracks> muted {};
    std::array<std::array<int, kParamSlots>, kTracks> params {};   // the tracks' SLOOP parameters (the knobs' values)
    std::array<Page, kTracks> page { { Page::mix, Page::macro, Page::macro, Page::macro } };
    std::vector<std::pair<int, int>> held;               // the steps held (row, step): knobs lock them
    std::array<Sound, kTracks> sounds = kDefaultSounds;   // DR: { -, kit }
    int seenParamSerial = -1;
    bool playing = false;
    bool recording = false;
    struct Pending { int track, slot, step; };
    std::vector<Pending> pendingRecord;                  // recorded on a step not played yet: sent once it has passed
    int bpm = 118;
    int swing = 0;                                       // 0..100 (SLOOP's SWING: 50 % straight .. 75 %)
    Key key;
    Prefs prefs;                                         // the app's settings (metronome, count-in, quantize)
    std::optional<Clip> clipboard;
    std::function<void()> changed;                       // every screen repaints

    static constexpr int kNone = -2, kStop = -1;

    Clip* editClip (int t)
    {
        auto& c = session.at (t, editSlot[(size_t) t]);
        return c ? &*c : nullptr;
    }
    Clip* editClip() { return editClip (sel); }
    int note (int t, int row) const { return rowNote (t, row, key); }   // a piano roll row's MIDI note
    bool editingPlaying() const { return audio.playingSlot[sel].load() == editSlot[(size_t) sel]; }
    void notify() { if (changed) changed(); }

    void select (int t) { sel = t; held.clear(); notify(); }
    void togglePlay()
    {
        playing = ! playing;
        audio.play (playing, playing && recording && prefs.countIn);   // (recording: a bar of clicks first)
        if (! playing)
            for (int t = 0; t < kTracks; ++t) queued[(size_t) t] = kNone;
        notify();
    }
    void setBpm (int b) { bpm = juce::jlimit (60, 200, b); audio.setBpm (bpm); notify(); }
    void setSwing (int v) { swing = juce::jlimit (0, 100, v); audio.setSwing (swing); notify(); }
    void setKey (Key k) { key = k; audio.setKey (key); notify(); }
    void setPrefs (const Prefs& p)
    {
        prefs = p;
        audio.setMetronome (prefs.metronome);
        store::writePrefs (prefs);
        notify();
    }
    void setSound (int t, int engine, int preset)
    {
        sounds[(size_t) t] = { engine, preset };
        if (isDrum (t)) audio.setKit (preset);
        else audio.setSound (t, engine, preset);
        notify();
    }
    void setMute (int t, bool m)
    {
        muted[(size_t) t] = m;
        params[(size_t) t][kParamMute] = m ? 1 : 0;
        audio.setParam (t, kParamMute, m ? 1 : 0);
        notify();
    }

    // ---- projects: what is saved, and loading one (the song stops; each track starts on its first row)
    Project snapshot() const
    {
        Project p;
        p.name = projectName;
        p.bpm = bpm;
        p.swing = swing;
        p.key = key;
        p.session = session;
        p.sounds = sounds;
        p.params = params;
        p.hasParams = true;
        return p;
    }
    void load (const Project& p)
    {
        if (playing) togglePlay();
        session = p.session;
        if (session.scenes() == 0) session.addScene();
        projectName = p.name;
        sel = 0;
        lane = 0;
        held.clear();
        pendingRecord.clear();
        recording = false;
        editSlot.fill (0);
        queued.fill (kNone);
        page = { { Page::mix, Page::macro, Page::macro, Page::macro } };
        undoStack.clear();
        redoStack.clear();
        setBpm (p.bpm);
        setSwing (p.swing);
        setKey (p.key);
        for (int t = 0; t < kTracks; ++t)
        {
            const auto snd = p.sounds[(size_t) t];
            sounds[(size_t) t] = snd;
            if (isDrum (t)) audio.setKit (snd.preset);
            else audio.setSound (t, snd.engine, snd.preset);
            if (p.hasParams)                             // (after the sound: the saved values over the preset's)
            {
                params[(size_t) t] = p.params[(size_t) t];
                audio.setParams (t, p.params[(size_t) t]);
            }
            muted[(size_t) t] = p.hasParams && p.params[(size_t) t][kParamMute] != 0;
            playlist[(size_t) t].clear();
            if (session.at (t, 0)) playlist[(size_t) t] = { 0 };
            sendPlaylist (t, AudioEngine::When::now);
        }
        notify();
    }

    // ---- session
    void launch (int t, int s)
    {
        if (! session.at (t, s)) return;
        editSlot[(size_t) t] = s;
        playlist[(size_t) t] = { s };
        queued[(size_t) t] = playing ? s : kNone;
        sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
        notify();
    }
    void chain (int t, const std::vector<int>& rows)
    {
        playlist[(size_t) t] = rows;
        editSlot[(size_t) t] = rows.front();
        queued[(size_t) t] = playing ? rows.front() : kNone;
        sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
        notify();
    }
    void stopTrack (int t)
    {
        playlist[(size_t) t].clear();
        queued[(size_t) t] = playing ? kStop : kNone;
        sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
    }
    void launchScene (int r)
    {
        for (int t = 0; t < kTracks; ++t)
        {
            if (session.at (t, r)) launch (t, r);
            else stopTrack (t);
        }
        notify();
    }
    void stopAll() { for (int t = 0; t < kTracks; ++t) stopTrack (t); notify(); }
    // the audio thread has taken a queued clip: no more blinking
    void poll()
    {
        bool any = false;
        if (audio.paramSerial.load() != seenParamSerial) { readParams(); any = true; }   // a sound brought its own values
        for (auto it = pendingRecord.begin(); it != pendingRecord.end();)
            if (audio.position[it->track].load() != it->step || ! playing)
            {
                clipEdited (it->track, it->slot);
                it = pendingRecord.erase (it);
            }
            else ++it;
        for (int t = 0; t < kTracks; ++t)
        {
            auto q = queued[(size_t) t];
            const int now = audio.playingSlot[t].load();
            if (q != kNone && (q == now || (q == kStop && now < 0))) { queued[(size_t) t] = kNone; any = true; }
        }
        if (any) notify();
    }
    // an edited clip: the engine gets it at once if it plays (or waits in a chain)
    void clipEdited (int t, int s)
    {
        auto& pl = playlist[(size_t) t];
        if (std::find (pl.begin(), pl.end(), s) != pl.end() && session.at (t, s))
            audio.setPlaylist (t, AudioEngine::When::update, { { s, &session.at (t, s)->data } });
        notify();
    }
    void createClip (int t, int s)
    {
        remember ("clip");
        session.at (t, s) = Session::newClip (t, "Clip " + juce::String (s + 1));
        editSlot[(size_t) t] = s;
        notify();
    }
    void deleteClip (int t, int s)
    {
        remember ("clip");
        session.at (t, s).reset();
        auto& pl = playlist[(size_t) t];
        if (std::find (pl.begin(), pl.end(), s) != pl.end()) stopTrack (t);
        notify();
    }
    void duplicateClip (int t, int s)
    {
        remember ("clip");
        int r = s + 1;
        while (r < session.scenes() && session.at (t, r)) ++r;
        if (r == session.scenes()) session.addScene();
        session.at (t, r) = session.at (t, s);
        session.at (t, r)->name << " bis";
        notify();
    }
    void addScene() { remember ("scene"); session.addScene(); notify(); }
    void insertSceneCopy (int r, bool capture)
    {
        remember ("scene");
        for (int t = 0; t < kTracks; ++t)
        {
            auto& col = session.cols[(size_t) t];
            const int from = capture ? audio.playingSlot[t].load() : r;
            std::optional<Clip> c = from >= 0 && from < (int) col.size() ? col[(size_t) from] : std::nullopt;
            col.insert (col.begin() + r + 1, c);
        }
        shiftRows (r + 1, 1);
        notify();
    }
    void deleteScene (int r)
    {
        if (session.scenes() <= 1) return;
        remember ("scene");
        for (int t = 0; t < kTracks; ++t)
        {
            auto& pl = playlist[(size_t) t];
            if (std::find (pl.begin(), pl.end(), r) != pl.end()) stopTrack (t);
            session.cols[(size_t) t].erase (session.cols[(size_t) t].begin() + r);
        }
        shiftRows (r + 1, -1);
        notify();
    }

    // copy / paste a clip (a drum clip onto the drum track, a synth clip onto a synth track)
    void copyClip (int t, int s) { if (session.at (t, s)) clipboard = session.at (t, s); notify(); }
    bool canPaste (int t) const { return clipboard && (clipboard->data.drum != 0) == isDrum (t); }
    void pasteClip (int t, int s)
    {
        if (! canPaste (t)) return;
        remember ("clip");
        session.at (t, s) = clipboard;
        editSlot[(size_t) t] = s;
        clipEdited (t, s);
    }
    // a clip's length (1..64 steps); twice: the clip played twice in a row, as one clip twice as long
    void setLength (int t, int s, int len)
    {
        auto& c = session.at (t, s);
        if (! c) return;
        remember ("length");
        c->data.length = (uint8_t) juce::jlimit (1, kMaxSteps, len);
        clipEdited (t, s);
    }
    void doubleClip (int t, int s)
    {
        auto& c = session.at (t, s);
        if (! c || c->data.length * 2 > kMaxSteps) return;
        remember ("length");
        auto& d = c->data;
        const int n = d.length;
        for (int i = 0; i < n; ++i)
        {
            for (auto& ln : d.lanes) ln[(size_t) (n + i)] = ln[(size_t) i];
            d.notes[(size_t) (n + i)] = d.notes[(size_t) i];
            d.noteLen[(size_t) (n + i)] = d.noteLen[(size_t) i];
        }
        const int locks = d.nLocks;
        for (int i = 0; i < locks; ++i)
            d.setLock (d.locks[(size_t) i].step + n, d.locks[(size_t) i].param, d.locks[(size_t) i].value);   // (while there is room)
        d.length = (uint8_t) (n * 2);
        clipEdited (t, s);
    }
    void quantizeClip (int t, int s)                     // every step back on the grid
    {
        auto& c = session.at (t, s);
        if (! c) return;
        remember ("quantize");
        for (auto& ln : c->data.lanes) for (auto& x : ln) x.nudge = 0;
        for (auto& x : c->data.notes) x.nudge = 0;
        clipEdited (t, s);
    }

    // ---- undo / redo: the session (clips, scenes) as it was before an edit. Edits of one kind close together
    // (a drag, a few taps) are one step back; a recording pass is one step back however long it is
    void remember (const char* kind)
    {
        const double t = juce::Time::getMillisecondCounterHiRes();
        const bool same = lastKind == kind && (lastKind == juce::String ("rec") || t - lastEdit < 400.0);
        lastEdit = t;
        lastKind = kind;
        if (same) return;
        undoStack.push_back ({ session, editSlot, playlist });
        if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo() { travel (undoStack, redoStack); }
    void redo() { travel (redoStack, undoStack); }

    // ---- steps
    void toggleStep (int row, int step, bool on)
    {
        auto* c = editClip();
        if (c == nullptr) return;
        remember ("step");
        auto& d = c->data;
        if (d.drum) d.lanes[(size_t) row][(size_t) step].on = on;
        else
        {
            auto& x = d.notes[(size_t) step];
            x.rows = on ? x.rows | 1u << row : x.rows & ~(1u << row);
            x.on = x.rows != 0;
            d.noteLen[(size_t) step][(size_t) row] = on ? 1 : 0;
        }
        clipEdited (sel, editSlot[(size_t) sel]);
    }
    int noteAt (int row, int step)                       // the start of the note sounding there (piano roll), else -1
    {
        auto* c = editClip();
        return c != nullptr && ! c->data.drum ? c->data.noteStart (row, step) : -1;
    }
    void setNoteLength (int row, int start, int len)
    {
        auto* c = editClip();
        if (c == nullptr || c->data.drum) return;
        len = juce::jlimit (1, c->data.maxLength (row, start), len);
        if (c->data.noteLength (start, row) == len) return;
        remember ("length");
        c->data.noteLen[(size_t) start][(size_t) row] = (uint8_t) len;
        stepChanged();
    }
    StepData* step (int row, int s)
    {
        auto* c = editClip();
        if (c == nullptr) return nullptr;
        return c->data.drum ? &c->data.lanes[(size_t) row][(size_t) s] : &c->data.notes[(size_t) s];
    }
    void stepChanged() { clipEdited (sel, editSlot[(size_t) sel]); }

    // ---- knobs: four SLOOP parameters of the selected track (its page), or the held steps' locks
    std::vector<Page> pages() const
    {
        if (isDrum (sel)) return { Page::mix, Page::fx };
        return { Page::mix, Page::macro, Page::edit1, Page::edit2, Page::env, Page::fx };
    }
    juce::String pageName (Page p) const
    {
        const int e = sounds[(size_t) sel].engine;
        switch (p)
        {
            case Page::mix: return "Mix";
            case Page::macro: return "Mac";
            case Page::edit1: return se_engine_page (e, 0);
            case Page::edit2: return se_engine_page (e, 1);
            case Page::env: return "Env";
            case Page::fx: return "FX";
        }
        return {};
    }
    std::array<KnobDef, kKnobs> knobDefs() const
    {
        static const int mix[] = { 0, 50, 36, 39 }, env[] = { 1, 2, 3, 4 }, fx[] = { 33, 34, 35, 36 };
        static const char* mixNames[] = { "Level", "Filtre", "Reverb", "Pan" };
        const int e = isDrum (sel) ? -1 : sounds[(size_t) sel].engine;
        std::array<KnobDef, kKnobs> d;
        for (int k = 0; k < kKnobs; ++k)
        {
            int id = -1;
            switch (page[(size_t) sel])
            {
                case Page::mix: id = mix[k]; break;
                case Page::macro: id = se_engine_macro (e, k); break;
                case Page::edit1: id = SE_P_E0 + k; break;
                case Page::edit2: id = SE_P_E0 + 4 + k; break;
                case Page::env: id = env[k]; break;
                case Page::fx: id = fx[k]; break;
            }
            const char* label = nullptr;
            auto& x = d[(size_t) k];
            if (id >= 0 && se_describe (e, id, &x.min, &x.max, &x.def, &label) && label != nullptr
                && juce::String (label) != "-" && x.max > x.min)
            {
                x.param = id;
                x.valid = true;
                x.label = page[(size_t) sel] == Page::mix ? juce::String (mixNames[k]) : juce::String (label);
            }
        }
        return d;
    }
    juce::String format (const KnobDef& d, int v) const
    {
        if (const char* n = se_value_name (isDrum (sel) ? -1 : sounds[(size_t) sel].engine, d.param, v)) return n;
        if (d.param == SE_P_TFLT) return v == 0 ? juce::String ("off") : (v < 0 ? "LP " : "HP ") + juce::String (std::abs (v));
        if (d.param == SE_P_PAN) return v == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (std::abs (v));
        return juce::String (v);
    }
    void setPage (Page p) { page[(size_t) sel] = p; notify(); }
    int knob (int k)
    {
        const auto d = knobDefs()[(size_t) k];
        if (! d.valid) return 0;
        if (! held.empty())
            if (auto* c = editClip())
                if (const auto v = c->data.lock (held.front().second, d.param); v != kNoLock) return v;
        return params[(size_t) sel][(size_t) d.param];
    }
    bool knobLocked (int k)
    {
        const auto d = knobDefs()[(size_t) k];
        auto* c = editClip();
        return d.valid && ! held.empty() && c != nullptr && c->data.lock (held.front().second, d.param) != kNoLock;
    }
    void setKnob (int k, int v)
    {
        const auto d = knobDefs()[(size_t) k];
        if (! d.valid) return;
        v = juce::jlimit (d.min, d.max, v);
        if (! held.empty())
        {
            if (auto* c = editClip())
            {
                remember ("lock");
                for (auto& [row, s] : held) c->data.setLock (s, d.param, v);
                lockedSinceHold = true;
                stepChanged();
            }
            return;
        }
        params[(size_t) sel][(size_t) d.param] = v;
        audio.setParam (sel, d.param, v);
        notify();
    }
    void resetKnob (int k)
    {
        const auto d = knobDefs()[(size_t) k];
        if (! d.valid) return;
        if (! held.empty())
        {
            remember ("lock");
            if (auto* c = editClip())
                for (auto& [row, s] : held) c->data.clearLock (s, d.param);
            lockedSinceHold = true;
            stepChanged();
            return;
        }
        setKnob (k, d.def);
    }

    // ---- live recording: a pad played while REC is on goes into the clip the track plays, on the nearest step
    // REC while stopped starts the song too (after a bar of clicks, if the count-in is on)
    void toggleRecord()
    {
        recording = ! recording;
        lastKind = {};                                   // (a new take: its own step back)
        if (recording && ! playing) togglePlay();
        else notify();
    }
    void recordHit (int row, int velocity)
    {
        if (! recording || ! playing || audio.countIn.load() > 0) return;
        const int t = sel;
        int slot = audio.playingSlot[t].load();
        remember ("rec");
        if (slot < 0)                                    // nothing plays on the track: a new clip, playing at once
        {
            slot = editSlot[(size_t) t];
            if (session.at (t, slot)) { slot = 0; while (slot < session.scenes() && session.at (t, slot)) ++slot; }
            if (slot == session.scenes()) session.addScene();
            session.at (t, slot) = Session::newClip (t, "Rec " + juce::String (slot + 1));
            editSlot[(size_t) t] = slot;
            playlist[(size_t) t] = { slot };
            sendPlaylist (t, AudioEngine::When::now);
            notify();
            return;                                       // (it starts with this bar: the next hits go in)
        }
        auto& clip = session.at (t, slot);
        if (! clip) return;
        const int pos = audio.position[t].load();
        if (pos < 0) return;
        const float phase = audio.stepPhase[t].load();
        const bool late = phase >= 0.5f;                 // nearer the next step
        const int step = late ? (pos + 1) % clip->data.length : pos;
        // not quantized: the hit keeps its place between the steps (a nudge, in sixths of a step)
        const int nudge = prefs.quantizeRec ? 0 : juce::roundToInt ((late ? phase - 1.0f : phase) * 6.0f);
        auto& d = clip->data;
        if (d.drum) { auto& x = d.lanes[(size_t) row][(size_t) step]; x.on = 1; x.vel = (uint8_t) velocity; x.nudge = (int8_t) juce::jlimit (-3, 3, nudge); }
        else
        {
            if (const int st = d.noteStart (row, step); st >= 0 && st < step)   // a held note there ends before it
                d.noteLen[(size_t) st][(size_t) row] = (uint8_t) (step - st);
            auto& x = d.notes[(size_t) step];
            x.rows |= 1u << row;
            x.on = 1;
            x.vel = (uint8_t) velocity;
            x.nudge = (int8_t) juce::jlimit (-3, 3, nudge);
            d.noteLen[(size_t) step][(size_t) row] = 1;
        }
        if (late) pendingRecord.push_back ({ t, slot, step });   // (it already sounded: not twice)
        else clipEdited (t, slot);
        notify();
    }
    bool lockedSinceHold = false;                        // a knob turned while steps were held

private:
    struct UndoState { Session session; std::array<int, kTracks> editSlot; std::array<std::vector<int>, kTracks> playlist; };
    std::vector<UndoState> undoStack, redoStack;
    juce::String lastKind;
    double lastEdit = 0;
    void travel (std::vector<UndoState>& from, std::vector<UndoState>& to)
    {
        if (from.empty()) return;
        to.push_back ({ session, editSlot, playlist });
        const auto u = std::move (from.back());
        from.pop_back();
        lastKind = {};
        held.clear();
        pendingRecord.clear();
        session = u.session;
        for (int t = 0; t < kTracks; ++t)
        {
            editSlot[(size_t) t] = juce::jlimit (0, session.scenes() - 1, u.editSlot[(size_t) t]);
            // what plays goes on, with the clips as they were (a clip that is gone stops)
            std::vector<int> pl;
            for (int s : u.playlist[(size_t) t])
                if (s < session.scenes() && session.at (t, s)) pl.push_back (s);
            if (pl != playlist[(size_t) t])
            {
                playlist[(size_t) t] = pl;
                queued[(size_t) t] = kNone;
                sendPlaylist (t, AudioEngine::When::now);
            }
            else sendPlaylist (t, AudioEngine::When::update);
        }
        notify();
    }
    void readParams()
    {
        seenParamSerial = audio.paramSerial.load();
        for (int t = 0; t < kTracks; ++t)
            for (int i = 0; i < kParamSlots; ++i) params[(size_t) t][(size_t) i] = audio.paramValue[t][i].load();
    }
    void sendPlaylist (int t, AudioEngine::When when)
    {
        std::vector<std::pair<int, const ClipData*>> clips;
        for (int s : playlist[(size_t) t])
            if (auto& c = session.at (t, s)) clips.push_back ({ s, &c->data });
        audio.setPlaylist (t, when, clips);
    }
    void shiftRows (int from, int d)
    {
        for (int t = 0; t < kTracks; ++t)
        {
            if (editSlot[(size_t) t] >= from) editSlot[(size_t) t] = juce::jmax (0, editSlot[(size_t) t] + d);
            for (auto& s : playlist[(size_t) t]) if (s >= from) s += d;
            if (queued[(size_t) t] >= from) queued[(size_t) t] += d;
        }
        // the audio thread reports rows: keep it in step with the new numbering
        audio.renumber (from, d);
    }
};

int runSelfTest();                                       // SelfTest.cpp: the app's --selftest
} // namespace sloop
