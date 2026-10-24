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

private:
    void timerCallback() override;
    int sceneW() const { return getWidth() < 420 ? 44 : 58; }
    int colW() const { return (getWidth() - sceneW() - kTracks * theme::gap) / kTracks; }
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
    void refresh() { grid.setSize (viewport.getMaximumVisibleWidth(), grid.wantedHeight()); grid.repaint(); repaint(); }

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
    int firstStep() const { return narrow() ? stepPage * 8 : 0; }
    int visibleSteps() const { return narrow() ? 8 : kSteps; }
    juce::Rectangle<int> gridArea() const { return getLocalBounds().withTrimmedTop (headH + theme::gap); }
    int labelW() const { return narrow() ? 62 : 84; }
    bool cellAt (juce::Point<int>, int& row, int& step) const;
    int labelAt (juce::Point<int>) const;
    void preview (int row);

    Controller& ctl;
    std::function<void (int, int)> onSheet;
    juce::TextButton lanes1 { "Sons 1-8" }, lanes2 { "9-16" }, steps1 { "Pas 1-8" }, steps2 { "9-16" };
    int lanePage = 0, stepPage = 0;
    struct Pointer { int row, step; juce::Point<int> at; enum Mode { pressing, painting, velocity } mode = pressing;
                     bool created = false, sheet = false, value = false; int vel0 = 100; double time = 0; };
    std::map<int, Pointer> pointers;
    static constexpr int headH = 34;
};

class PadsView : public juce::Component
{
public:
    explicit PadsView (Controller& c) : ctl (c) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> padsArea() const;
    int padAt (juce::Point<int>) const;
    int padSound (int pad) const { return (3 - pad / 4) * 4 + pad % 4; }   // the first sound bottom left, as an MPC
    Controller& ctl;
    std::map<int, std::pair<int, float>> down;                             // finger -> (pad, velocity)
};

class KnobStrip : public juce::Component
{
public:
    explicit KnobStrip (Controller& c) : ctl (c) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> knobRect (int k) const;
    int knobAt (juce::Point<int>) const;
    static juce::String format (int k, int v);
    Controller& ctl;
    struct Drag { int k = -1; float y = 0, moved = 0, value = 0; };
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
