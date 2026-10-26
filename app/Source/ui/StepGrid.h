// SPDX-License-Identifier: GPL-3.0-only
// The step grid (the drum track's lanes) and the piano roll (a synth track's rows, the key's degrees): steps,
// long notes, held steps for locks, the clip's length.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"
#include <map>

namespace sloop
{
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
} // namespace sloop
