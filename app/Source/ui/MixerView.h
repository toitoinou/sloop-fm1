// SPDX-License-Identifier: GPL-3.0-only
// The mixer: a strip per track (the audio track's too) and the master.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"
#include <map>

namespace sloop
{
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
    static constexpr int kMaster = kTracks + 1;          // the master's strip (the others: the tracks shown, Controller::columns)
    int strips() const { return (int) ctl.columns().size(); }
    int column (int strip) const
    {
        const auto c = ctl.columns();
        const auto it = std::find (c.begin(), c.end(), strip);
        return it == c.end() ? -1 : (int) (it - c.begin());
    }
    struct Hit { int strip = -1; Part part = none; };
    void timerCallback() override;
    int masterW() const { return juce::jmin (130, getWidth() / 4); }
    int shown() const { return juce::jlimit (juce::jmin (4, strips()), strips(), (getWidth() - masterW()) / 70); }
    juce::Rectangle<int> stripRect (int strip) const;
    juce::Rectangle<int> partRect (int strip, Part) const;
    Hit hitAt (juce::Point<int>) const;
    // a control's value, range and setter (a track's: its parameter or the drum track's global; the master's)
    struct Control { int value = 0, min = 0, max = 127, def = 0; std::function<void (int)> set; juce::String label; bool valid = false; };
    Control control (int strip, Part) const;
    Controller& ctl;
    int first = 0;
    juce::File recording;
    std::array<float, kMaster + 1> meter {};
    struct Drag { Hit hit; float start = 0; int value0 = 0; };
    std::map<int, Drag> drags;
    std::map<int, double> lastTap;
};
} // namespace sloop
