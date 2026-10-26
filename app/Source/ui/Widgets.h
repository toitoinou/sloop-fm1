// SPDX-License-Identifier: GPL-3.0-only
// Small widgets: the icon buttons (undo, redo), a button held (FILL), the tempo box.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"

namespace sloop
{
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
