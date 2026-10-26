// SPDX-License-Identifier: GPL-3.0-only
#include "ui/TrackRail.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

juce::Rectangle<int> TrackRail::slot (int t) const
{
    auto r = getLocalBounds();
    const bool vertical = getHeight() > getWidth();
    const int n = kTracks, g = theme::gap * 3;
    if (vertical && (r.getHeight() - g * (n - 1)) / n < 46)   // (a phone: two columns of four)
    {
        const int rows = (n + 1) / 2, w = (r.getWidth() - g) / 2, h = (r.getHeight() - g * (rows - 1)) / rows;
        return { (t / rows) * (w + g), (t % rows) * (h + g), w, h };
    }
    if (vertical)
    {
        const int h = (r.getHeight() - g * (n - 1)) / n;
        return { 0, t * (h + g), r.getWidth(), h };
    }
    const int w = (r.getWidth() - g * (n - 1)) / n;
    return { t * (w + g), 0, w, r.getHeight() };
}

int TrackRail::trackAt (juce::Point<int> p) const
{
    for (int t = 0; t < kTracks; ++t) if (slot (t).contains (p)) return t;
    return -1;
}

void TrackRail::paint (juce::Graphics& g)
{
    for (int t = 0; t < kTracks; ++t)
    {
        auto r = slot (t).toFloat();
        const bool sel = t == ctl.sel, muted = ctl.muted[(size_t) t];
        g.setOpacity (1.0f);
        g.setColour (sel ? theme::cellBeat : theme::panel);
        g.fillRoundedRectangle (r, theme::radius);
        g.setColour (theme::line);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius, 1.0f);
        auto text = r.reduced (12, 8).toNearestInt();
        const float alpha = muted ? 0.42f : 1.0f;
        g.setColour ((sel ? theme::track (t) : theme::fg).withMultipliedAlpha (alpha));
        g.setFont (font (r.getHeight() > 70 ? 20.0f : 17.0f, true));
        g.drawText (juce::String (trackId (t)) + (muted ? " M" : ""), text.removeFromTop (text.getHeight() / 2 + 4),
                    juce::Justification::bottomLeft);
        if (r.getHeight() > 56)
        {
            g.setColour (theme::dim.withMultipliedAlpha (alpha));
            g.setFont (font (12.0f));
            g.drawText (trackName (t), text, juce::Justification::topLeft);
        }
        g.setColour (theme::track (t).withAlpha (0.3f + 0.7f * led[(size_t) t]));
        g.fillEllipse (r.getRight() - 17, r.getY() + 9, 8, 8);
    }
}

void TrackRail::timerCallback()
{
    bool dirty = false;
    for (int t = 0; t < kTracks; ++t)
    {
        const int h = ctl.audio.hits[t].load();
        if (h != seenHits[(size_t) t]) { seenHits[(size_t) t] = h; led[(size_t) t] = 1.0f; dirty = true; }
        else if (led[(size_t) t] > 0.0f) { led[(size_t) t] = juce::jmax (0.0f, led[(size_t) t] - 0.25f); dirty = true; }
    }
    if (downTrack >= 0 && ! longDone && now() - downTime > kLongPress)
    {
        longDone = true;
        ctl.setMute (downTrack, ! ctl.muted[(size_t) downTrack]);
    }
    if (dirty) repaint();
}

void TrackRail::mouseDown (const juce::MouseEvent& e)
{
    downTrack = trackAt (e.getPosition());
    downTime = now();
    longDone = false;
}

void TrackRail::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > kMoveSlop) downTrack = -1;
}

void TrackRail::mouseUp (const juce::MouseEvent&)
{
    if (downTrack >= 0 && ! longDone) ctl.select (downTrack);
    downTrack = -1;
}
} // namespace sloop
