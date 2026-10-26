// SPDX-License-Identifier: GPL-3.0-only
// The session: a column per track (the audio track last), a row per scene. A tap launches a clip at the bar,
// a double tap edits it, a long press opens its menu; two fingers on a track's clips chain them.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"
#include <map>

namespace sloop
{
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
    static constexpr int kColumns = kTracks + 1;        // the tracks, then the audio track (kAudio)
    // the tracks in sight: all of them when they fit (a column 76 points or more), else a page of them
    int sceneW() const { return getWidth() < 420 ? 44 : 58; }
    int shown() const { return juce::jlimit (4, kColumns, (getWidth() - sceneW()) / 78); }
    int colW() const { return (getWidth() - sceneW() - shown() * theme::gap) / shown(); }
    int first = 0;                                   // the first track in sight
    std::function<void (int)> onAudioEdit, onAudioImport;   // the audio track: a clip's editor; a sound into a row
    void page (int direction)
    {
        first = juce::jlimit (0, juce::jmax (0, kColumns - shown()), first + direction * shown());
        repaint();
    }

private:
    void timerCallback() override;
    juce::Rectangle<int> cellRect (int t, int r) const;
    juce::Rectangle<int> sceneRect (int r) const;
    juce::Rectangle<int> addRect() const;
    void clipMenu (int t, int s);
    void audioMenu (int s);
    void sceneMenu (int r);
    void paintAudio (juce::Graphics&, int r, bool blink);

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
    SessionGrid& sessionGrid() { return grid; }
    void refresh()
    {
        grid.setSize (viewport.getMaximumVisibleWidth(), grid.wantedHeight());
        if (ctl.sel < grid.first || ctl.sel >= grid.first + grid.shown())   // (the selected track in sight)
            grid.first = juce::jmin (SessionGrid::kColumns - grid.shown(), ctl.sel / grid.shown() * grid.shown());
        grid.repaint();
        repaint();
    }

private:
    Controller& ctl;
    SessionGrid grid;
    juce::Viewport viewport;
    static constexpr int headH = 38;
};
} // namespace sloop
