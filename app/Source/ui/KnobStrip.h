// SPDX-License-Identifier: GPL-3.0-only
// The knobs: four parameters of the selected track, page by page; the sound's name and its neighbours.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"
#include <map>

namespace sloop
{
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
} // namespace sloop
