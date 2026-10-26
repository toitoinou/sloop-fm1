// SPDX-License-Identifier: GPL-3.0-only
#include "ui/KnobStrip.h"
#include "ui/Common.h"
#include "ui/SoundSheet.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

// the page tabs: one row, or two when there are many of them for the width (ANALOG's eight on a phone)
int KnobStrip::pageRows() const { return (int) ctl.pages().size() * 50 > getWidth() - 20 ? 2 : 1; }

juce::Rectangle<int> KnobStrip::pagesRect() const
{
    return getLocalBounds().reduced (10, 8).withTrimmedTop (26).removeFromTop (26 * pageRows());
}

juce::Rectangle<int> KnobStrip::pageRect (int i) const
{
    auto r = pagesRect();
    const int n = (int) ctl.pages().size(), rows = pageRows(), perRow = (n + rows - 1) / rows;
    const int w = r.getWidth() / juce::jmax (1, perRow);
    return { r.getX() + (i % perRow) * w, r.getY() + (i / perRow) * 26, w - 3, 24 };
}

juce::Rectangle<int> KnobStrip::knobRect (int k) const
{
    auto a = getLocalBounds().reduced (10).withTrimmedTop (60 + 26 * (pageRows() - 1));
    if (getWidth() < 360 && getHeight() > 200)        // the narrow right column: 2 x 2
    {
        const int w = a.getWidth() / 2, h = juce::jmin (a.getHeight() / 2, w + 20);
        return { a.getX() + (k % 2) * w, a.getY() + (k / 2) * h, w, h };
    }
    const int w = a.getWidth() / kKnobs;
    return { a.getX() + k * w, a.getY(), w, a.getHeight() };
}

int KnobStrip::knobAt (juce::Point<int> p) const
{
    for (int k = 0; k < kKnobs; ++k) if (knobRect (k).contains (p)) return k;
    return -1;
}

void KnobStrip::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::radius);
    const auto col = theme::track (ctl.sel);
    auto title = titleRect();
    g.setColour (col);
    g.fillRect (title.getX(), title.getCentreY() - 7, 3, 14);
    g.setColour (theme::fg);
    g.setFont (font (16.0f, true));
    const auto& snd = ctl.sounds[(size_t) ctl.sel];
    juce::String name = isDrum (ctl.sel) ? se_kit_name (snd.preset) : se_preset_name (snd.engine, snd.preset);
    if (! ctl.held.empty()) name << juce::String::fromUTF8 (" · pas ") << (ctl.held.front().second + 1);
    else name << juce::String::fromUTF8 ("  ▾");
    g.drawText (name, title.withTrimmedLeft (10).withRight (soundArrow (0).getX() - 4), juce::Justification::centredLeft, true);
    for (int i = 0; i < 2; ++i)                         // < >: the sound before / after, without the sheet
    {
        auto r = soundArrow (i).toFloat();
        g.setColour (theme::cellBeat);
        g.fillRoundedRectangle (r, theme::radius);
        g.setColour (theme::fg);
        g.setFont (font (14.0f, true));
        g.drawText (i == 0 ? "<" : ">", r, juce::Justification::centred);
    }

    const auto pages = ctl.pages();
    for (int i = 0; i < (int) pages.size(); ++i)
    {
        auto r = pageRect (i).toFloat();
        const bool on = pages[(size_t) i] == ctl.page[(size_t) ctl.sel];
        g.setColour (on ? theme::cellBeat : theme::panel);
        g.fillRoundedRectangle (r, theme::radius);
        if (on) { g.setColour (col); g.fillRect (r.removeFromBottom (2.0f)); }
        g.setColour (on ? theme::fg : theme::dim);
        g.setFont (font (11.5f, on));
        g.drawText (ctl.pageName (pages[(size_t) i]), pageRect (i), juce::Justification::centred, true);
    }

    const auto defs = ctl.knobDefs();
    for (int k = 0; k < kKnobs; ++k)
    {
        auto r = knobRect (k);
        const auto& d = defs[(size_t) k];
        const int v = ctl.knob (k);
        const float frac = d.valid ? (float) (v - d.min) / (float) (d.max - d.min) : 0.0f;
        const float size = juce::jmin ((float) r.getWidth() - 8.0f, (float) r.getHeight() - 22.0f, 64.0f);
        auto circle = juce::Rectangle<float> (size, size).withCentre ({ (float) r.getCentreX(), (float) r.getY() + size / 2 + 2 });
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        juce::Path track, value;
        track.addCentredArc (circle.getCentreX(), circle.getCentreY(), size / 2 - 3, size / 2 - 3, 0, a0, a1, true);
        value.addCentredArc (circle.getCentreX(), circle.getCentreY(), size / 2 - 3, size / 2 - 3, 0, a0,
                             a0 + (a1 - a0) * juce::jmax (0.001f, frac), true);
        const bool locked = ctl.knobLocked (k);
        g.setColour (juce::Colour (0xff4a4a4a).withAlpha (d.valid ? 1.0f : 0.35f));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (! d.valid) continue;
        g.setColour (locked ? theme::fg : col);
        g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::fg);
        g.setFont (font (size < 50 ? 10.5f : 12.0f));
        g.drawText (ctl.format (d, v), circle.reduced (4.0f), juce::Justification::centred, true);
        g.setColour (theme::dim);
        g.drawText (d.label, juce::Rectangle<float> ((float) r.getX(), circle.getBottom() + 2, (float) r.getWidth(), 16.0f),
                    juce::Justification::centredTop);
        if (locked)
        {
            auto badge = juce::Rectangle<float> (circle.getRight() - 14, circle.getY() - 4, 30, 13);
            g.setColour (theme::fg);
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (theme::panel);
            g.setFont (font (9.0f, true));
            g.drawText ("LOCK", badge, juce::Justification::centred);
        }
    }
}

void KnobStrip::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 2; ++i)
        if (soundArrow (i).contains (e.getPosition()) && ctl.held.empty()) { stepSound (ctl, i == 0 ? -1 : 1); return; }
    if (titleRect().contains (e.getPosition()) && ctl.held.empty()) { if (onSound) onSound(); return; }
    const auto pages = ctl.pages();
    for (int i = 0; i < (int) pages.size(); ++i)
        if (pageRect (i).contains (e.getPosition())) { ctl.setPage (pages[(size_t) i]); return; }
    Drag d;
    d.k = knobAt (e.getPosition());
    d.y = (float) e.getPosition().y;
    d.x = (float) e.getPosition().x;
    if (d.k >= 0 && ! ctl.knobDefs()[(size_t) d.k].valid) d.k = -1;
    if (d.k >= 0) d.value = (float) ctl.knob (d.k);
    drags[e.source.getIndex()] = d;
}

void KnobStrip::mouseDrag (const juce::MouseEvent& e)
{
    auto it = drags.find (e.source.getIndex());
    if (it == drags.end() || it->second.k < 0) return;
    auto& d = it->second;
    const auto def = ctl.knobDefs()[(size_t) d.k];
    // up or right turns it up: the whole range in about 150 points (a short swipe); a short range (a wave, a mode:
    // a few values) one value per 24 points; a second finger on the strip: four times finer
    const float dy = d.y - (float) e.getPosition().y, dx = (float) e.getPosition().x - d.x;
    d.y = (float) e.getPosition().y;
    d.x = (float) e.getPosition().x;
    const float delta = std::abs (dy) >= std::abs (dx) ? dy : dx;
    d.moved += std::abs (delta);
    if (d.moved < 3) return;
    const bool fine = drags.size() > 1;
    const float span = (float) (def.max - def.min);
    const float travel = (span <= 12.0f ? span * 24.0f : 150.0f) * (fine ? 4.0f : 1.0f);
    d.value += delta * span / travel;
    d.value = juce::jlimit ((float) def.min, (float) def.max, d.value);
    ctl.setKnob (d.k, juce::roundToInt (d.value));
}

void KnobStrip::mouseUp (const juce::MouseEvent& e)
{
    auto it = drags.find (e.source.getIndex());
    if (it == drags.end()) return;
    const auto d = it->second;
    drags.erase (it);
    if (d.k < 0 || d.moved >= 4) return;
    if (now() - lastTap[(size_t) d.k] < 300.0) { ctl.resetKnob (d.k); lastTap[(size_t) d.k] = 0; }
    else lastTap[(size_t) d.k] = now();
}
} // namespace sloop
