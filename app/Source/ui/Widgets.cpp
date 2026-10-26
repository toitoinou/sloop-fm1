// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Widgets.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

void IconButton::paintButton (juce::Graphics& g, bool over, bool isDown)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isDown ? theme::panel.brighter (0.15f) : over ? theme::panel.brighter (0.05f) : theme::panel);
    g.fillRoundedRectangle (r, theme::radius);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r, theme::radius, 1.0f);
    // a curved arrow: an arc over the top, its head pointing back down (undo: to the left; redo: mirrored)
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.42f, cx = r.getCentreX(), cy = r.getCentreY() + s * 0.15f;
    juce::Path arc;
    arc.addCentredArc (cx, cy, s * 0.62f, s * 0.55f, 0.0f, -juce::MathConstants<float>::halfPi * 1.25f,
                       juce::MathConstants<float>::halfPi * 0.9f, true);
    juce::Path head;
    const float hx = cx - s * 0.62f * std::sin (juce::MathConstants<float>::halfPi * 1.25f), hy = cy - s * 0.55f * std::cos (juce::MathConstants<float>::halfPi * 1.25f);
    head.addTriangle (hx - s * 0.32f, hy - s * 0.05f, hx + s * 0.22f, hy - s * 0.12f, hx - s * 0.02f, hy + s * 0.38f);
    const auto flip = this->icon == Icon::redo ? juce::AffineTransform::scale (-1.0f, 1.0f, cx, cy) : juce::AffineTransform();
    g.setColour ((isEnabled() ? theme::fg : theme::dim).withAlpha (isEnabled() ? 1.0f : 0.5f));
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), flip);
    g.fillPath (head, flip);
}

void BpmBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, theme::radius);
    g.setColour (theme::fg);
    g.setFont (font (17.0f));
    g.drawText (juce::String (ctl.bpm), r.withTrimmedBottom (r.getHeight() * 0.3f), juce::Justification::centredBottom);
    g.setColour (theme::dim);
    g.setFont (font (9.5f));
    g.drawText (ctl.linkPeers < 0 ? juce::String ("BPM") : "LINK " + juce::String (ctl.linkPeers),   // (Link: the apps with it)
                r.withTrimmedTop (r.getHeight() * 0.68f), juce::Justification::centredTop);
}
} // namespace sloop
