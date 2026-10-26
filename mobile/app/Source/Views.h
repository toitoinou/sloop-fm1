// SPDX-License-Identifier: GPL-3.0-only
// The screens, after the mockup (mobile/mockup/sloop-touch.html): the track rail, the session (clips and scenes),
// the step grid / piano roll, the pads, the knobs and the step sheet. Touch first: every gesture follows one
// finger (MouseEvent::source), so several fingers can act at once (hold a step + turn a knob, hold a clip + tap
// others).
#pragma once
#include "Controller.h"
#include "Theme.h"
#include <map>

namespace sloop
{
class TrackRail : public juce::Component, private juce::Timer
{
public:
    explicit TrackRail (Controller& c) : ctl (c) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> slot (int t) const;
    int trackAt (juce::Point<int>) const;
    Controller& ctl;
    std::array<int, kTracks> seenHits {};
    std::array<float, kTracks> led {};
    int downTrack = -1;
    double downTime = 0;
    bool longDone = false;
};

class SessionGrid : public juce::Component, private juce::Timer
{
public:
    SessionGrid (Controller& c, std::function<void (int, int)> edit) : ctl (c), onEdit (std::move (edit)) { startTimerHz (30); }
    int wantedHeight() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int rowH = 58, addH = 44;
    // the tracks in sight: all of them when they fit (a column 76 points or more), else a page of them
    int sceneW() const { return getWidth() < 420 ? 44 : 58; }
    int shown() const { return juce::jlimit (4, kTracks, (getWidth() - sceneW()) / 78); }
    int colW() const { return (getWidth() - sceneW() - shown() * theme::gap) / shown(); }
    int first = 0;                                   // the first track in sight
    void page (int direction)
    {
        first = juce::jlimit (0, juce::jmax (0, kTracks - shown()), first + direction * shown());
        repaint();
    }

private:
    void timerCallback() override;
    juce::Rectangle<int> cellRect (int t, int r) const;
    juce::Rectangle<int> sceneRect (int r) const;
    juce::Rectangle<int> addRect() const;
    void clipMenu (int t, int s);
    void sceneMenu (int r);

    Controller& ctl;
    std::function<void (int, int)> onEdit;
    struct Press { int source = -1, t = -1, s = -1, scene = -1; std::vector<int> list; bool chained = false, done = false;
                   double time = 0; juce::Point<int> at; };
    Press press;
    std::map<int, double> lastTap;
};

class SessionView : public juce::Component
{
public:
    SessionView (Controller& c, std::function<void (int, int)> edit);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh()
    {
        grid.setSize (viewport.getMaximumVisibleWidth(), grid.wantedHeight());
        if (ctl.sel < grid.first || ctl.sel >= grid.first + grid.shown())   // (the selected track in sight)
            grid.first = juce::jmin (kTracks - grid.shown(), ctl.sel / grid.shown() * grid.shown());
        grid.repaint();
        repaint();
    }

private:
    Controller& ctl;
    SessionGrid grid;
    juce::Viewport viewport;
    static constexpr int headH = 38;
};

class StepGrid : public juce::Component, private juce::Timer
{
public:
    StepGrid (Controller& c, std::function<void (int, int)> sheet);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    bool narrow() const { return getWidth() < 560; }
    std::vector<int> rows() const;
    int length() const;                              // the clip's steps (a bar without a clip)
    int visibleSteps() const { return narrow() ? 8 : kBar; }
    int stepPages() const { return (length() + visibleSteps() - 1) / visibleSteps(); }
    int firstStep() const { return juce::jlimit (0, stepPages() - 1, stepPage) * visibleSteps(); }
    int shownRows() const { return gridArea().getHeight() < 300 ? 12 : 16; }   // the piano roll's window
    void lengthMenu();
    juce::Rectangle<int> gridArea() const { return getLocalBounds().withTrimmedTop (headH + theme::gap); }
    int labelW() const { return (int) ((narrow() ? 62.0f : 84.0f) * theme::textScale); }
    bool cellAt (juce::Point<int>, int& row, int& step) const;
    int columnAt (int x) const;                      // the step under x (clamped to the visible ones)
    int labelAt (juce::Point<int>) const;
    void preview (int row);

    Controller& ctl;
    std::function<void (int, int)> onSheet;
    juce::TextButton lanes1 { "Sons 1-8" }, lanes2 { "9-16" }, steps1 { "<" }, steps2 { ">" }, lengthButton;
    int lanePage = 0, stepPage = 0, rowBase = 0;
    struct Pointer { int row, step; juce::Point<int> at; enum Mode { pressing, painting, velocity, stretching } mode = pressing;
                     bool created = false, sheet = false, value = false; int vel0 = 100; double time = 0; };
    std::map<int, Pointer> pointers;
    static constexpr int headH = 34;
};

// The pads. The drums: 4 x 4, as Quest for Groove lays them out (the kick and the snare doubled, for two hands),
// or the 16 sounds in order from the bottom left as an MPC. A synth: a keyboard in the key, in fourths as a Push
// or a Launchpad (a row up: three degrees of the scale up), only the scale's notes, the roots lit; CHORDS plays
// the triad of the scale on each pad
class PadsView : public juce::Component
{
public:
    explicit PadsView (Controller& c) : ctl (c) {}
    std::function<void()> onKey;                     // the key button tapped (the settings)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> padsArea() const { return getLocalBounds().withTrimmedTop (36); }
    juce::Rectangle<int> headButton (int i) const;   // drums: QFG, MPC; a synth: Oct -, Oct +, Accords, the key
    int cols() const { return isDrum (ctl.sel) ? 4 : getWidth() >= 520 ? 8 : 5; }
    int padAt (juce::Point<int>) const;
    int padSound (int pad) const
    {
        static constexpr int qfgLanes[16] = { 9, 14, 10, 11,      // low tom, mid tom (conga), high tom, crash
                                              4, 5, 4, 12,        // hat, open hat, hat, ride
                                              7, 2, 2, 7,         // rim (sidestick), snare, snare, rim
                                              3, 0, 0, 13 };      // clap, kick, kick, shaker
        return qfg ? qfgLanes[pad] : (3 - pad / 4) * 4 + pad % 4;
    }
    int padRow (int pad) const;                      // a synth pad's degree (piano roll row), -1 out of range
    std::vector<int> padRows (int pad) const;        // the rows it plays (a chord: three)
    Controller& ctl;
    bool qfg = true, chords = false;
    int octave = 0;
    struct Down { int pad; float vel; std::vector<int> notes, rows; int track; std::vector<int> repeats = {}; };
    static constexpr int kRepeat = 10, kRate = 11;   // the header's REPEAT and rate buttons (headButton)
    std::map<int, Down> down;                        // finger -> what it plays
};

// The mixer: a strip per track (its name: select it; mute, solo; pan; reverb and delay sends; its level and
// meter), the master on the right (DJ filter, DUST, DUCK, the volume and the output's meter). A drag moves a
// control (up / right: more), a double tap puts it back. A narrow screen shows a page of strips (< >)
class MixerView : public juce::Component, private juce::Timer
{
public:
    explicit MixerView (Controller& c) : ctl (c) { startTimerHz (30); }
    std::function<void (const juce::File&)> onRecorded;   // a recording of the output, just finished
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum Part { name, mute, solo, pan, rev, dly, fader, pagePrev, pageNext, record, none };
    struct Hit { int strip = -1; Part part = none; };   // strip kTracks: the master
    void timerCallback() override;
    int masterW() const { return juce::jmin (130, getWidth() / 4); }
    int shown() const { return juce::jlimit (4, kTracks, (getWidth() - masterW()) / 70); }
    juce::Rectangle<int> stripRect (int strip) const;
    juce::Rectangle<int> partRect (int strip, Part) const;
    Hit hitAt (juce::Point<int>) const;
    // a control's value, range and setter (a track's: its parameter or the drum track's global; the master's)
    struct Control { int value = 0, min = 0, max = 127, def = 0; std::function<void (int)> set; juce::String label; bool valid = false; };
    Control control (int strip, Part) const;
    Controller& ctl;
    int first = 0;
    juce::File recording;
    std::array<float, kTracks + 1> meter {};
    struct Drag { Hit hit; float start = 0; int value0 = 0; };
    std::map<int, Drag> drags;
    std::map<int, double> lastTap;
};

// a small button drawn as an icon: undo, redo
class IconButton : public juce::Button
{
public:
    enum class Icon { undo, redo };
    explicit IconButton (Icon i) : juce::Button (i == Icon::undo ? "Annuler" : "Refaire"), icon (i) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
};

class KnobStrip : public juce::Component
{
public:
    explicit KnobStrip (Controller& c) : ctl (c) {}
    std::function<void()> onSound;                   // the title (the track's sound) tapped
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> knobRect (int k) const;
    juce::Rectangle<int> titleRect() const { return getLocalBounds().reduced (12, 6).removeFromTop (26); }
    juce::Rectangle<int> soundArrow (int i) const { return titleRect().removeFromRight (30 * (2 - i) + 4 * (1 - i)).removeFromLeft (30); }
    juce::Rectangle<int> pagesRect() const;
    int pageRows() const;
    juce::Rectangle<int> pageRect (int i) const;
    int knobAt (juce::Point<int>) const;
    Controller& ctl;
    struct Drag { int k = -1; float x = 0, y = 0, moved = 0, value = 0; };
    std::map<int, Drag> drags;
    std::array<double, kKnobs> lastTap {};
};

// the long-press sheet of a step: probability, condition, ratchet, nudge, velocity, locks
class StepSheet : public juce::Component
{
public:
    StepSheet (Controller& c, int row, int step, std::function<void()> close);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void sync();
    juce::Rectangle<int> panel() const;
    Controller& ctl;
    int row, step;
    std::function<void()> onClose;
    juce::OwnedArray<juce::TextButton> prob, cond, ratchet;
    juce::TextButton early { "< avant" }, late { juce::String::fromUTF8 ("après >") }, clearLocks { "Effacer les locks" }, del { "Supprimer" },
        ok { "OK" };
    juce::Slider vel;
};

// the sound of the selected track, on the whole screen: on the left the kinds of sound (from every engine) then the
// engines, on the right their sounds; a tap plays one at once (a note, or a few hits for a kit), < > the one before /
// after; the sheet stays open to try the next. DR: its kits
class SoundSheet : public juce::Component
{
public:
    SoundSheet (Controller& c, std::function<void()> close);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Item { int engine, preset; };                 // engine -1: a kit
    struct Row { juce::String label; int kind, index; }; // kind 0 a heading, 1 a kind of sound, 2 an engine
    struct List : juce::Component
    {
        explicit List (SoundSheet& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        SoundSheet& owner;
    };
    struct Grid : juce::Component
    {
        explicit Grid (SoundSheet& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        juce::Rectangle<int> tile (int i) const;
        int cols() const { return juce::jmax (2, getWidth() / 170); }
        SoundSheet& owner;
    };
    void showEngine (int engine);
    void showCategory (int category);
    void pick (int index);
    int current() const;                                 // the item that plays now, -1 none in this list
    Controller& ctl;
    std::function<void()> onClose;
    int engineShown = 0, categoryShown = -1;             // a kind of sound across the engines, or one engine
    std::vector<Row> rows;
    std::vector<Item> items;
    List list { *this };
    Grid grid { *this };
    juce::Viewport listView, gridView;
    juce::TextButton prev { "<" }, next { ">" }, ok { "OK" };
    static constexpr int rowH = 40, tileH = 54;
};

void auditionSound (Controller&);                        // the track's sound, briefly (a note, a few drum hits)
void stepSound (Controller&, int direction);             // the track's next / previous preset (its engine's), or kit

// the settings: the key and its scale, the swing; the app's metronome, count-in, recording quantize; the audio
class SettingsSheet : public juce::Component
{
public:
    SettingsSheet (Controller& c, std::function<void()> close, std::function<juce::String()> audioInfo,
                   std::function<void()> nextBuffer, std::function<juce::String()> midiInfo);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void sync();
    juce::Rectangle<int> panel() const;
    Controller& ctl;
    std::function<void()> onClose;
    std::function<juce::String()> audioInfo, midiInfo;
    juce::Component content;
    juce::Viewport view;
    juce::OwnedArray<juce::TextButton> roots, scaleButtons;
    juce::Slider swing;
    juce::TextButton metronome { juce::String::fromUTF8 ("Métronome") }, countIn { juce::String::fromUTF8 ("Décompte 1 mesure") },
        quantize { juce::String::fromUTF8 ("Enregistrer sur la grille") }, buffer, ok { "OK" },
        midiInputs, bluetooth { juce::String::fromUTF8 ("Bluetooth MIDI…") }, clockOut { juce::String::fromUTF8 ("Horloge MIDI (sortie)") };
    struct Label : juce::Component
    {
        juce::String text;
        void paint (juce::Graphics&) override;
    };
    juce::OwnedArray<Label> labels;
};

juce::String engineAbout (const juce::String& engineName);   // an engine in a line
int soundCategoryCount();                                     // the kinds of sound (Basses, Leads, Nappes...)
juce::String soundCategoryName (int category);
int soundCategory (const juce::String& presetName);           // a preset's kind, from its name

// a button that is on only while a finger holds it (FILL)
class HoldButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void (bool)> onHold;
    void mouseDown (const juce::MouseEvent& e) override { setToggleState (true, juce::dontSendNotification); if (onHold) onHold (true); juce::TextButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { setToggleState (false, juce::dontSendNotification); if (onHold) onHold (false); juce::TextButton::mouseUp (e); }
};

// the tempo: drag up / down
class BpmBox : public juce::Component
{
public:
    explicit BpmBox (Controller& c) : ctl (c) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { start = ctl.bpm; }
    void mouseDrag (const juce::MouseEvent& e) override { ctl.setBpm (start - e.getDistanceFromDragStartY() / 4); }

private:
    Controller& ctl;
    int start = 118;
};
} // namespace sloop
