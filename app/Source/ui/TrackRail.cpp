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
    const int n = ctl.trackCount + 1, g = theme::gap * 3;        // (the tracks and the "+")
    if (vertical && (r.getHeight() - g * (n - 1)) / n < 46)   // (a phone: two columns)
    {
        const int rows = (n + 1) / 2, w = (r.getWidth() - g) / 2, h = juce::jmin (84, (r.getHeight() - g * (rows - 1)) / rows);
        return { (t / rows) * (w + g), (t % rows) * (h + g), w, h };
    }
    if (vertical)
    {
        const int h = juce::jmin (84, (r.getHeight() - g * (n - 1)) / n);   // (few tracks: not as tall as the rail)
        return { 0, t * (h + g), r.getWidth(), t == n - 1 ? juce::jmin (h, 44) : h };
    }
    const int w = juce::jmin (140, (r.getWidth() - g * (n - 1)) / n);
    return { t * (w + g), 0, t == n - 1 ? juce::jmin (w, 52) : w, r.getHeight() };
}

int TrackRail::trackAt (juce::Point<int> p) const
{
    for (int t = 0; t <= ctl.trackCount; ++t) if (slot (t).contains (p)) return t;
    return -1;
}

void TrackRail::paint (juce::Graphics& g)
{
    {                                                    // "+": a track more, the audio track
        auto r = slot (ctl.trackCount).toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, theme::radius);
        g.setColour (theme::dim);
        g.setFont (font (18.0f, true));
        g.drawText ("+", r, juce::Justification::centred);
    }
    for (int t = 0; t < ctl.trackCount; ++t)
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
    if (downTrack >= 0 && downTrack < ctl.trackCount && ! longDone && now() - downTime > kLongPress)
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
    if (downTrack == ctl.trackCount) tracksMenu();
    else if (downTrack >= 0 && ! longDone) ctl.select (downTrack);
    downTrack = -1;
}

void TrackRail::tracksMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Pistes");
    if (ctl.canAddTrack())
        m.addItem (1, juce::String::fromUTF8 ("Ajouter ") + trackId (ctl.trackCount) + " (" + trackName (ctl.trackCount) + ")");
    if (! ctl.audioShown) m.addItem (2, "Ajouter la piste audio (AU)");
    if (ctl.trackCount > kMinTracks)
        m.addItem (3, juce::String ("Retirer ") + trackId (ctl.trackCount - 1) + juce::String::fromUTF8 (" (ses clips s'effacent)"));
    if (ctl.audioShown) m.addItem (4, juce::String::fromUTF8 ("Retirer la piste audio (ses clips s'effacent)"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (slot (ctl.trackCount))),
                     [this] (int r)
                     {
                         if (r == 1) ctl.addTrack();
                         if (r == 2) ctl.showAudioTrack (true);
                         if (r == 3) ctl.removeTrack();
                         if (r == 4) ctl.showAudioTrack (false);
                         repaint();
                     });
}
} // namespace sloop
