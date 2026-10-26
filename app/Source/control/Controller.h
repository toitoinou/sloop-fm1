// SPDX-License-Identifier: GPL-3.0-only
// What the screens share and every edit they make: the session, the selection, what plays and what waits for the
// bar, the knobs, recording, the mix, the audio track, undo, projects. It turns the screens' gestures into the
// audio engine's commands (AudioEngine) and reads back what the audio thread reports (poll, 30 times a second).
// One class, its parts in their own files: Controller.cpp (transport, projects, poll, undo), Session.cpp, Knobs.cpp,
// Recording.cpp, Mixer.cpp, AudioTrack.cpp. The message thread only.
#pragma once
#include "audio/AudioEngine.h"
#include "control/UndoHistory.h"
#include "model/Project.h"
#include "model/Samples.h"
#include "sloop_engine.h"
#include <functional>

namespace sloop
{
class Controller
{
public:
    static constexpr int kNone = -2, kStop = -1;         // a track's waiting row: none, or its stop

    // restore: reopen the autosave of the last time (else the demo); the self-test starts from the demo
    explicit Controller (AudioEngine&, bool restore = true);

    AudioEngine& audio;
    std::function<void()> changed;                       // every screen repaints
    void notify() { if (changed) changed(); }
    void poll();                                         // what the audio thread did (the clips that started, Link...)

    // ---- the song: transport, tempo, swing, key, the app's settings
    bool playing = false;
    int bpm = 118;
    int swing = 0;                                       // 0..100 (SLOOP's SWING: 50 % straight .. 75 %)
    Key key;
    Prefs prefs;                                         // the app's settings (metronome, count-in, quantize, MIDI, Link)
    int linkPeers = -1;                                  // the other Link apps (-1: Link off)
    void togglePlay();
    void setBpm (int b) { bpm = juce::jlimit (kMinBpm, kMaxBpm, b); audio.setBpm (bpm); notify(); }
    void setSwing (int v) { swing = juce::jlimit (0, 100, v); audio.setSwing (swing); notify(); }
    void setKey (Key k) { key = k; audio.setKey (key); notify(); }
    void setPrefs (const Prefs&);

    // ---- projects: what is saved, and loading one (the song stops; each track starts on its first row)
    juce::String projectName;
    Project snapshot() const;
    void load (const Project&);

    // ---- the tracks: the one selected, their sounds and their SLOOP parameters
    int sel = 0;                                         // the selected track
    int lane = 0;                                        // the drum sound the knobs and pads point at
    std::array<Sound, kTracks> sounds = kDefaultSounds;  // DR: { -, kit }
    std::array<std::array<int, kParamSlots>, kTracks> params {};   // as the engine has them (the knobs' values)
    void select (int t) { sel = t; held.clear(); audio.midiTrack = t; notify(); }
    void setSound (int t, int engine, int preset);

    // ---- the session: clips and scenes, what each track plays (a chain of rows) and what waits for the bar
    Session session;
    std::array<int, kTracks> editSlot {};                // the clip each track edits
    std::array<int, kTracks> queued = filled (kNone);    // a row waiting for the bar, kStop, kNone
    std::array<std::vector<int>, kTracks> playlist;      // the rows each track plays in turn
    std::optional<Clip> clipboard;
    Clip* editClip (int t)
    {
        auto& c = session.at (t, editSlot[(size_t) t]);
        return c ? &*c : nullptr;
    }
    Clip* editClip() { return editClip (sel); }
    bool editingPlaying() const { return audio.playingSlot[sel].load() == editSlot[(size_t) sel]; }
    void launch (int t, int s);
    void chain (int t, const std::vector<int>& rows);
    void stopTrack (int t);
    void launchScene (int r);
    void stopAll();
    void clipEdited (int t, int s);                      // an edited clip: the engine gets it at once if it plays
    void createClip (int t, int s);
    void deleteClip (int t, int s);
    void duplicateClip (int t, int s);
    void copyClip (int t, int s) { if (session.at (t, s)) clipboard = session.at (t, s); notify(); }
    bool canPaste (int t) const { return clipboard && (clipboard->data.drum != 0) == isDrum (t); }   // (drums on drums)
    void pasteClip (int t, int s);
    void setLength (int t, int s, int len);              // 1..64 steps
    void doubleClip (int t, int s);                      // played twice in a row, as one clip twice as long
    void quantizeClip (int t, int s);                    // every step back on the grid
    void addScene() { remember ("scene"); session.addScene(); notify(); }
    void insertSceneCopy (int r, bool capture);          // capture: what plays now, else row r
    void deleteScene (int r);

    // ---- the steps of the clip edited (the grid, the piano roll, the step sheet)
    std::vector<std::pair<int, int>> held;               // the steps held (row, step): knobs lock them
    bool lockedSinceHold = false;                        // a knob turned while steps were held
    int note (int t, int row) const { return rowNote (t, row, key); }   // a piano roll row's MIDI note
    void toggleStep (int row, int step, bool on);
    int noteAt (int row, int step);                      // the start of the note sounding there, else -1
    void setNoteLength (int row, int start, int len);
    StepData* step (int row, int s);
    void stepChanged() { clipEdited (sel, editSlot[(size_t) sel]); }

    // ---- undo / redo of the session (clips, scenes, what plays): Edits of one kind close together are one step;
    // a recording pass is one step however long it is
    void remember (const char* kind);
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();

    // ---- the knobs: four parameters of the selected track (its page), or the held steps' locks (Knobs.cpp)
    static constexpr int kGlobal = 1000;                 // a knob's parameter at kGlobal + id: a global setting
    std::array<Page, kTracks> page = firstPages();
    std::vector<Page> pages() const;
    juce::String pageName (Page) const;
    std::array<KnobDef, kKnobs> knobDefs() const;
    juce::String format (const KnobDef&, int value) const;
    void setPage (Page p) { page[(size_t) sel] = p; notify(); }
    int knob (int k);
    bool knobLocked (int k);
    void setKnob (int k, int value);
    void resetKnob (int k);

    // ---- recording (Recording.cpp): pads, keys and MIDI into the clip that plays, knob moves as locks, note repeat
    bool recording = false;
    bool repeatOn = false;
    int repeatRate = 2;                                  // repeatSteps: 1/16
    void toggleRecord();                                 // (stopped: the song starts too, after a count-in)
    void recordHit (int row, int velocity);
    void recordRelease (int t, int row);                 // a key let go: the note it recorded lasts until here
    void recordMotion (int param, int value);
    void clearMotion (int t, int s);                     // every lock of a clip
    void repeatPress (int t, int row, int id, int velocity);
    void repeatRelease (int t, int id);
    void midiNote (int note, int velocity);              // (MIDI in: the audio thread already plays it)
    void midiCC (int cc, int value);                     // CC 21..24 and 70..73: the four knobs

    // ---- the mix (Mixer.cpp): the tracks' level, pan and sends are their parameters (the drum track's: global
    // settings); mute, solo; the master volume, SLOOP's master bus; the audio track's own mix
    static constexpr int kMixGlobals[] = { SE_G_DRLVL, SE_G_DRREV, SE_G_DRDLY, SE_G_DUST, SE_G_DUCK, SE_G_FILT,
                                           SE_G_RSIZE, SE_G_DTIME, SE_G_DFDBK };
    std::array<int, 48> globals {};
    std::array<bool, kTracks> muted {};
    int masterVolume = 100;                              // 0..127, 100: unity
    unsigned soloMask = 0;                               // bit t: track t (kAudio: the audio track)
    int audioLevel = 90, audioPan = 0;                   // the audio track's fader (0..127) and pan (-64..63)
    bool audioMuted = false;
    void setGlobal (int id, int value);
    void setMasterVolume (int value);
    void setTrackParam (int t, int id, int value);       // (no lock, no move recorded)
    void setMute (int t, bool muted);
    void toggleSolo (int t);
    void setAudioMix (int level, int pan, bool mute);

    // ---- the audio track (AudioTrack.cpp): a sample per row, played alone, launched as the other clips
    SampleBank samples;
    int audioRow = -1;                                   // its row playing (or waiting), -1 none
    int audioQueued = kNone;                             // its row waiting for the bar, kStop, kNone
    ClipPlayer::Clip playerClip (const AudioClip&);      // a clip as the audio thread plays it
    void launchAudio (int s);
    void stopAudio();
    void audioEdited (int s);                            // the clip as it is now, wherever it plays or waits
    void setAudioClip (int s, const AudioClip&);         // an edit (the clip editor: a drag is one step back)
    void deleteAudio (int s);
    void duplicateAudio (int s);
    // a sound file (a file, a content:// URL, a recording of the app) as a new clip on row s
    bool importAudio (int s, const juce::URL& from, juce::String& error);

private:
    static std::array<int, kTracks> filled (int v) { std::array<int, kTracks> a; a.fill (v); return a; }
    static std::array<Page, kTracks> firstPages() { std::array<Page, kTracks> a; a.fill (Page::macro); a[0] = Page::mix; return a; }

    struct UndoState { Session session; std::array<int, kTracks> editSlot; std::array<std::vector<int>, kTracks> playlist; int audioRow; };
    UndoHistory<UndoState> history;
    void restore (UndoState);

    // what poll() follows
    int seenParamSerial = -1, seenLinkSerial = 0;
    double seenLinkTempo = 0;
    bool seenLinkPlaying = false;

    // recording: notes whose keys are still down, hits on steps not played yet, repeats held
    struct Pending { int track, slot, step; };
    struct RecHeld { int track, slot, step, row; };
    struct HeldRepeat { int track, row, id, vel, lastBarStep; };
    std::vector<Pending> pendingRecord;                  // sent once the step has passed (not heard twice)
    std::vector<RecHeld> recordedHeld;
    std::vector<HeldRepeat> heldRepeats;
    void recordRepeats();

    void readParams();
    void sendPlaylist (int t, AudioEngine::When);
    void shiftRows (int from, int delta);                // rows from `from` on moved by delta
};

int runSelfTest();                                       // SelfTest.cpp: the app's --selftest
} // namespace sloop
