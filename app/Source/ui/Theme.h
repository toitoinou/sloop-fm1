// SPDX-License-Identifier: GPL-3.0-only
// The "Studio" look of the mockup: flat graphite, thin lines, colour kept for the music (the tracks' colours).
#pragma once
#include "model/Model.h"

namespace sloop::theme
{
inline const juce::Colour bg { 0xff151618 }, panel { 0xff1e1f22 }, cell { 0xff28292d }, cellBeat { 0xff2f3035 },
    line { 0xff0b0c0d }, fg { 0xffdddbd5 }, dim { 0xff7b7e85 }, ink { 0xff121314 }, play { 0xff7cc48f },
    rec { 0xffe0645a };
inline juce::Colour track (int t) { return trackColour (t); }   // (Model.h: the tracks' own colours)
constexpr float radius = 4.0f;
constexpr int gap = 2;
// the text grows on a phone (MainComponent::resized sets it from the screen): sizes are in dp there, and 12 dp
// is too small to read at arm's length
inline float textScale = 1.0f;
inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h * textScale, bold ? juce::Font::bold : juce::Font::plain));
}

// buttons drawn flat, the "on" state inverted, as the mockup's .ctl / .ctl.on
struct LookAndFeel : juce::LookAndFeel_V4
{
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId, panel);
        setColour (juce::TextButton::buttonOnColourId, fg);
        setColour (juce::TextButton::textColourOffId, fg);
        setColour (juce::TextButton::textColourOnId, ink);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, fg);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cellBeat);
        setColour (juce::PopupMenu::highlightedTextColourId, fg);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId)
                                    : b.findColour (juce::TextButton::buttonColourId);
        if (down) c = c.brighter (0.15f);
        else if (over) c = c.brighter (0.05f);
        if (c.isTransparent()) return;                  // a plain text button (the audio info)
        g.setColour (c);
        g.fillRoundedRectangle (r, radius);
        g.setColour (line);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        if (b.findColour (juce::TextButton::buttonColourId).isTransparent()) return font (12.0f);
        return font (juce::jmin (15.0f, (float) h * 0.42f / textScale), true);
    }
};
} // namespace sloop::theme
