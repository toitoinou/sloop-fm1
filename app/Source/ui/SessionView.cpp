// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SessionView.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

juce::Rectangle<int> SessionGrid::cellRect (int t, int r) const
{
    const int v = column (t) - first;
    if (column (t) < 0 || v < 0 || v >= shown()) return {};   // (a track not shown, or on another page)
    return { v * (colW() + theme::gap), r * (rowH + theme::gap), colW(), rowH };
}

juce::Rectangle<int> SessionGrid::sceneRect (int r) const
{
    return { getWidth() - sceneW(), r * (rowH + theme::gap), sceneW(), rowH };
}

juce::Rectangle<int> SessionGrid::addRect() const
{
    return { 0, ctl.session.scenes() * (rowH + theme::gap), getWidth(), addH };
}

int SessionGrid::wantedHeight() const { return ctl.session.scenes() * (rowH + theme::gap) + addH + theme::gap; }

void SessionGrid::paint (juce::Graphics& g)
{
    const bool blink = std::fmod (now(), 450.0) < 225.0;
    for (int r = 0; r < ctl.session.scenes(); ++r)
    {
        paintAudio (g, r, blink);
        for (int t = 0; t < kTracks; ++t)
        {
            if (cellRect (t, r).isEmpty()) continue;
            auto box = cellRect (t, r).toFloat();
            auto& clip = ctl.session.at (t, r);
            if (! clip)
            {
                juce::Path p;
                p.addRoundedRectangle (box.reduced (1.0f), theme::radius);
                const float dash[] = { 4.0f, 4.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dash, 2);
                g.setColour (theme::cellBeat);
                g.fillPath (dashed);
                g.setColour (theme::dim);
                g.setFont (font (16.0f));
                g.drawText ("+", box, juce::Justification::centred);
                continue;
            }
            const bool playing = ctl.audio.playingSlot[t].load() == r;
            const bool queued = ctl.queued[(size_t) t] == r;
            const bool stopping = ctl.queued[(size_t) t] == Controller::kStop && playing;
            const auto col = theme::track (t);
            float alpha = (queued || stopping) && ! blink ? 0.4f : 1.0f;
            g.setColour (mixed (col, playing ? 0.62f : 0.24f).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (box, theme::radius);
            if (t == ctl.sel && ctl.editSlot[(size_t) t] == r)
            {
                g.setColour (theme::fg);
                g.drawRoundedRectangle (box.reduced (1.0f), theme::radius, 2.0f);
            }
            auto inner = box.reduced (8.0f, 7.0f);
            g.setColour (playing ? theme::ink : theme::fg);
            g.setFont (font (12.5f, true));
            g.drawText (clip->name, inner.removeFromTop (16.0f), juce::Justification::topLeft, true);
            // the clip's steps, small
            auto strip = inner.removeFromBottom (9.0f);
            const int len = clip->data.length;
            const float w = strip.getWidth() / (float) len;
            for (int s = 0; s < len; ++s)
            {
                const bool on = clip->data.columnActive (s);
                g.setColour (on ? (playing ? theme::ink : col) : (playing ? theme::ink : theme::fg).withAlpha (0.14f));
                g.fillRect (strip.getX() + (float) s * w, strip.getY(), juce::jmax (1.0f, w - (len > 32 ? 0.0f : 1.0f)), strip.getHeight());
            }
            if (len != kBar)
            {
                g.setColour ((playing ? theme::ink : theme::dim));
                g.setFont (font (10.0f));
                g.drawText (juce::String (len), box.reduced (6.0f, 5.0f).removeFromBottom (24.0f).removeFromTop (12.0f),
                            juce::Justification::topRight);
            }
            if (playing && ctl.playing)
            {
                const int pos = ctl.audio.position[t].load();
                g.setColour (theme::ink.withAlpha (0.8f));
                g.fillRect (box.getX(), box.getBottom() - 3.0f, box.getWidth() * (float) (pos + 1) / (float) len, 3.0f);
            }
            // its place in a chain
            const std::vector<int>& list = press.chained && press.t == t ? press.list : ctl.playlist[(size_t) t];
            if (list.size() > 1)
            {
                juce::String order;
                for (size_t i = 0; i < list.size(); ++i)
                    if (list[i] == r) order << (order.isEmpty() ? "" : ".") << (int) (i + 1);
                if (order.isNotEmpty())
                {
                    auto badge = juce::Rectangle<float> (box.getRight() - 30, box.getY() + 6, 24, 15);
                    g.setColour (theme::fg);
                    g.fillRoundedRectangle (badge, 3.0f);
                    g.setColour (theme::panel);
                    g.setFont (font (10.5f, true));
                    g.drawText (order, badge, juce::Justification::centred);
                }
            }
        }
        auto sc = sceneRect (r).toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (sc, theme::radius);
        g.setColour (theme::fg);
        juce::Path tri;
        tri.addTriangle (sc.getCentreX() - 10, sc.getCentreY() - 5, sc.getCentreX() - 10, sc.getCentreY() + 5,
                         sc.getCentreX() - 2, sc.getCentreY());
        g.fillPath (tri);
        g.setFont (font (13.0f));
        g.drawText (juce::String (r + 1), sc.withTrimmedLeft (sc.getWidth() / 2 - 2), juce::Justification::centredLeft);
    }
    auto add = addRect().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (add, theme::radius);
    g.setColour (theme::dim);
    g.setFont (font (15.0f, true));
    g.drawText (juce::String::fromUTF8 ("+ Scène"), add, juce::Justification::centred);
}

// the audio track's cell: the clip's name, its beats, where it is in its loop
void SessionGrid::paintAudio (juce::Graphics& g, int r, bool blink)
{
    if (cellRect (kAudio, r).isEmpty()) return;
    auto box = cellRect (kAudio, r).toFloat();
    const auto& clip = ctl.session.audio[(size_t) r];
    if (! clip)
    {
        juce::Path p;
        p.addRoundedRectangle (box.reduced (1.0f), theme::radius);
        const float dash[] = { 4.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dash, 2);
        g.setColour (theme::cellBeat);
        g.fillPath (dashed);
        g.setColour (theme::dim);
        g.setFont (font (11.0f));
        g.drawText (juce::String::fromUTF8 ("+ son"), box, juce::Justification::centred);
        return;
    }
    const bool playing = ctl.audio.audioSlot.load() == r;
    const bool waiting = ctl.audioQueued == r || (ctl.audioQueued == Controller::kStop && playing);
    const auto col = theme::track (kAudio);
    g.setColour (mixed (col, playing ? 0.62f : 0.24f).withMultipliedAlpha (waiting && ! blink ? 0.4f : 1.0f));
    g.fillRoundedRectangle (box, theme::radius);
    auto inner = box.reduced (8.0f, 7.0f);
    g.setColour (playing ? theme::ink : theme::fg);
    g.setFont (font (12.5f, true));
    g.drawText (clip->name, inner.removeFromTop (16.0f), juce::Justification::topLeft, true);
    g.setFont (font (10.5f));
    g.setColour (playing ? theme::ink : theme::dim);
    g.drawText (juce::String (clip->beats()) + " t  " + juce::String (juce::roundToInt (clip->bpm)) + (clip->stretch ? "" : " ~"),
                inner.removeFromTop (14.0f), juce::Justification::topLeft, true);
    if (playing && ctl.playing)
    {
        g.setColour (theme::ink.withAlpha (0.8f));
        g.fillRect (box.getX(), box.getBottom() - 3.0f, box.getWidth() * ctl.audio.audioProgress.load(), 3.0f);
    }
}

void SessionGrid::audioMenu (int s)
{
    juce::PopupMenu m;
    const auto& clip = ctl.session.audio[(size_t) s];
    if (! clip) return;
    m.addSectionHeader (clip->name);
    m.addItem (1, juce::String::fromUTF8 ("Éditer"));
    m.addItem (2, "Dupliquer dessous");
    m.addItem (4, juce::String::fromUTF8 ("Remplacer le son…"));
    m.addItem (3, "Effacer");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                         localAreaToGlobal (cellRect (kAudio, s))),
                     [this, s] (int r)
                     {
                         if (r == 1 && onAudioEdit) onAudioEdit (s);
                         if (r == 2) ctl.duplicateAudio (s);
                         if (r == 3) ctl.deleteAudio (s);
                         if (r == 4 && onAudioImport) onAudioImport (s);
                     });
}

void SessionGrid::timerCallback()
{
    if (press.source >= 0 && ! press.done && ! press.chained && now() - press.time > kLongPress)
    {
        press.done = true;
        if (press.scene >= 0) sceneMenu (press.scene);
        else if (press.t == kAudio) audioMenu (press.s);
        else if (press.t >= 0 && (ctl.session.at (press.t, press.s) || ctl.canPaste (press.t))) clipMenu (press.t, press.s);
    }
    repaint();
}

void SessionGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int src = e.source.getIndex();
    int t = -1, s = -1;
    for (int r = 0; r < ctl.session.scenes() && t < 0; ++r)
        for (int k : ctl.columns())
            if (cellRect (k, r).contains (p)) { t = k; s = r; break; }
    if (press.source >= 0 && press.source != src)
    {
        // a second finger while a clip is held: the chain grows (same track only)
        if (t == press.t && t >= 0 && t < kTracks && ctl.session.at (t, s)) { press.list.push_back (s); press.chained = true; }
        return;
    }
    press = {};
    press.source = src;
    press.time = now();
    press.at = p;
    if (t >= 0) { press.t = t; press.s = s; press.list = { s }; return; }
    for (int r = 0; r < ctl.session.scenes(); ++r)
        if (sceneRect (r).contains (p)) { press.scene = r; return; }
    if (addRect().contains (p)) { press.done = true; ctl.addScene(); }
}

void SessionGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (e.source.getIndex() == press.source && e.getDistanceFromDragStart() > kMoveSlop) press = {};   // a scroll
}

void SessionGrid::mouseUp (const juce::MouseEvent& e)
{
    if (e.source.getIndex() != press.source) return;
    auto p = press;
    press = {};
    if (p.done) return;
    if (p.scene >= 0) { ctl.launchScene (p.scene); return; }
    if (p.t < 0) return;
    if (p.t == kAudio)                                   // the audio track: no chain; an empty row takes a sound
    {
        if (! ctl.session.audio[(size_t) p.s]) { if (onAudioImport) onAudioImport (p.s); return; }
        const int key = kAudio * 10000 + p.s;
        if (now() - lastTap[key] < kDoubleTap) { lastTap[key] = 0; if (onAudioEdit) onAudioEdit (p.s); return; }
        lastTap[key] = now();
        ctl.launchAudio (p.s);
        return;
    }
    if (p.chained) { ctl.chain (p.t, p.list); return; }
    if (! ctl.session.at (p.t, p.s)) { ctl.createClip (p.t, p.s); ctl.select (p.t); return; }
    const int key = p.t * 10000 + p.s;
    if (now() - lastTap[key] < kDoubleTap) { lastTap[key] = 0; onEdit (p.t, p.s); return; }
    lastTap[key] = now();
    ctl.launch (p.t, p.s);
    if (p.t != ctl.sel) ctl.select (p.t);
}

void SessionGrid::clipMenu (int t, int s)
{
    juce::PopupMenu m;
    const auto& clip = ctl.session.at (t, s);
    if (clip)
    {
        m.addSectionHeader (clip->name);
        m.addItem (1, juce::String::fromUTF8 ("Éditer"));
        m.addItem (4, "Copier");
    }
    if (ctl.canPaste (t)) m.addItem (5, clip ? juce::String::fromUTF8 ("Coller (remplace)") : juce::String ("Coller ici"));
    if (clip)
    {
        m.addItem (2, "Dupliquer dessous");
        m.addItem (6, juce::String::fromUTF8 ("Doubler la longueur"), clip->data.length * 2 <= kMaxSteps);
        m.addItem (7, "Quantifier");
        m.addItem (8, "Effacer les mouvements", clip->data.nLocks > 0);
        m.addItem (3, "Effacer");
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                         localAreaToGlobal (cellRect (t, s))),
                     [this, t, s] (int r)
                     {
                         if (r == 1) onEdit (t, s);
                         if (r == 2) ctl.duplicateClip (t, s);
                         if (r == 3) ctl.deleteClip (t, s);
                         if (r == 4) ctl.copyClip (t, s);
                         if (r == 5) ctl.pasteClip (t, s);
                         if (r == 6) ctl.doubleClip (t, s);
                         if (r == 7) ctl.quantizeClip (t, s);
                         if (r == 8) ctl.clearMotion (t, s);
                     });
}

void SessionGrid::sceneMenu (int r)
{
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("Scène ") + juce::String (r + 1));
    m.addItem (1, "Dupliquer dessous");
    m.addItem (2, "Capturer ce qui joue");
    m.addItem (3, "Supprimer la ligne");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                         localAreaToGlobal (sceneRect (r))),
                     [this, r] (int k)
                     {
                         if (k == 1) ctl.insertSceneCopy (r, false);
                         if (k == 2) ctl.insertSceneCopy (r, true);
                         if (k == 3) ctl.deleteScene (r);
                     });
}

SessionView::SessionView (Controller& c, std::function<void (int, int)> edit) : ctl (c), grid (c, std::move (edit))
{
    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (false, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (viewport);
}

void SessionView::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (headH + theme::gap));
    refresh();
}

void SessionView::paint (juce::Graphics& g)
{
    const int sceneW = grid.sceneW(), colW = grid.colW();
    const auto columns = ctl.columns();
    for (int c = grid.first; c < grid.first + grid.shown() && c < (int) columns.size(); ++c)
    {
        const int t = columns[(size_t) c];
        juce::Rectangle<int> r ((c - grid.first) * (colW + theme::gap), 0, colW, headH);
        g.setColour (theme::track (t));
        g.setFont (font (14.0f, true));
        g.drawText (t == kAudio ? "AU" : trackId (t), r.withTrimmedLeft (8), juce::Justification::centredLeft);
        auto stop = r.removeFromRight (34).reduced (2, 4).toFloat();
        const bool off = t == kAudio ? ctl.audio.audioSlot.load() < 0 : ctl.audio.playingSlot[t].load() < 0;
        const bool stopping = t == kAudio ? ctl.audioQueued == Controller::kStop : ctl.queued[(size_t) t] == Controller::kStop;
        g.setColour (stopping ? theme::fg : theme::cell);
        g.fillRoundedRectangle (stop, theme::radius);
        g.setColour (theme::dim.withAlpha (off ? 0.35f : 1.0f));
        g.fillRect (stop.withSizeKeepingCentre (9, 9));
    }
    g.setColour (theme::dim);
    auto corner = juce::Rectangle<int> (getWidth() - sceneW, 0, sceneW, headH);
    if (grid.shown() < grid.columns())                   // the pages of tracks: < >
    {
        for (int i = 0; i < 2; ++i)
        {
            auto b = (i == 0 ? corner.withWidth (sceneW / 2) : corner.withTrimmedLeft (sceneW / 2)).reduced (2, 4).toFloat();
            const bool can = i == 0 ? grid.first > 0 : grid.first + grid.shown() < grid.columns();
            g.setColour (theme::cellBeat.withAlpha (can ? 1.0f : 0.35f));
            g.fillRoundedRectangle (b, theme::radius);
            g.setColour (theme::fg.withAlpha (can ? 1.0f : 0.35f));
            g.setFont (font (13.0f, true));
            g.drawText (i == 0 ? "<" : ">", b, juce::Justification::centred);
        }
        return;
    }
    g.setFont (font (11.0f));
    g.drawText (juce::String::fromUTF8 ("Scènes"), corner, juce::Justification::centred);
}

void SessionView::mouseUp (const juce::MouseEvent& e)
{
    if (e.getPosition().y > headH || e.mouseWasDraggedSinceMouseDown()) return;
    const int sceneW = grid.sceneW(), colW = grid.colW();
    if (e.getPosition().x >= getWidth() - sceneW)
    {
        if (grid.shown() < grid.columns()) grid.page (e.getPosition().x < getWidth() - sceneW / 2 ? -1 : 1);
        repaint();
        return;
    }
    const int c = grid.first + e.getPosition().x / (colW + theme::gap);
    const auto columns = ctl.columns();
    if (c >= grid.first + grid.shown() || c >= (int) columns.size()) return;
    const int t = columns[(size_t) c];
    if (t == kAudio) ctl.stopAudio();
    else ctl.stopTrack (t);
    ctl.notify();
}
} // namespace sloop
