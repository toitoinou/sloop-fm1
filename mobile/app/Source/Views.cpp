// SPDX-License-Identifier: GPL-3.0-only
#include "Views.h"
#include "sloop_engine.h"

namespace sloop
{
namespace
{
using theme::font;
constexpr double kLongPress = 480.0, kDoubleTap = 320.0;
constexpr int kMoveSlop = 12;
double now() { return juce::Time::getMillisecondCounterHiRes(); }
juce::Colour mixed (juce::Colour c, float amount, juce::Colour onto = theme::cell) { return onto.interpolatedWith (c, amount); }
const char* kLaneNames[kLanes] = { "Kick", "Kick 2", "Snare", "Clap", "Hat", "Open hat", "Pedal", "Rim",
                                   "Snare 2", "Tom bas", "Tom haut", "Crash", "Ride", "Shaker", "Conga", "Cowbell" };
const char* kTrackIds[kTracks] = { "DR", "T1", "T2", "T3" };
const char* kTrackNames[kTracks] = { "Drums", "Bass", "Keys", "Lead" };
juce::String condLabel (Cond c)
{
    switch (c)
    {
        case Cond::oneOfTwo: return "1:2";
        case Cond::twoOfTwo: return "2:2";
        case Cond::oneOfFour: return "1:4";
        case Cond::fill: return "F";
        case Cond::notFill: return "!F";
        case Cond::always: break;
    }
    return {};
}
} // namespace

// =================================================================== the track rail
juce::Rectangle<int> TrackRail::slot (int t) const
{
    auto r = getLocalBounds();
    const bool vertical = getHeight() > getWidth();
    const int n = kTracks, g = theme::gap * 3;
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
        g.drawText (juce::String (kTrackIds[t]) + (muted ? " M" : ""), text.removeFromTop (text.getHeight() / 2 + 4),
                    juce::Justification::bottomLeft);
        if (r.getHeight() > 56)
        {
            g.setColour (theme::dim.withMultipliedAlpha (alpha));
            g.setFont (font (12.0f));
            g.drawText (kTrackNames[t], text, juce::Justification::topLeft);
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

// =================================================================== the session
juce::Rectangle<int> SessionGrid::cellRect (int t, int r) const
{
    return { t * (colW() + theme::gap), r * (rowH + theme::gap), colW(), rowH };
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
        for (int t = 0; t < kTracks; ++t)
        {
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

void SessionGrid::timerCallback()
{
    if (press.source >= 0 && ! press.done && ! press.chained && now() - press.time > kLongPress)
    {
        press.done = true;
        if (press.scene >= 0) sceneMenu (press.scene);
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
        for (int k = 0; k < kTracks; ++k)
            if (cellRect (k, r).contains (p)) { t = k; s = r; break; }
    if (press.source >= 0 && press.source != src)
    {
        // a second finger while a clip is held: the chain grows (same track only)
        if (t == press.t && t >= 0 && ctl.session.at (t, s)) { press.list.push_back (s); press.chained = true; }
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
    const int sceneW = getWidth() < 420 ? 44 : 58;
    const int colW = (getWidth() - sceneW - kTracks * theme::gap) / kTracks;
    for (int t = 0; t < kTracks; ++t)
    {
        juce::Rectangle<int> r (t * (colW + theme::gap), 0, colW, headH);
        g.setColour (theme::track (t));
        g.setFont (font (14.0f, true));
        g.drawText (kTrackIds[t], r.withTrimmedLeft (8), juce::Justification::centredLeft);
        auto stop = r.removeFromRight (34).reduced (2, 4).toFloat();
        const bool off = ctl.audio.playingSlot[t].load() < 0;
        g.setColour (ctl.queued[(size_t) t] == Controller::kStop ? theme::fg : theme::cell);
        g.fillRoundedRectangle (stop, theme::radius);
        g.setColour (theme::dim.withAlpha (off ? 0.35f : 1.0f));
        g.fillRect (stop.withSizeKeepingCentre (9, 9));
    }
    g.setColour (theme::dim);
    g.setFont (font (11.0f));
    g.drawText (juce::String::fromUTF8 ("Scènes"), juce::Rectangle<int> (getWidth() - sceneW, 0, sceneW, headH), juce::Justification::centred);
}
void SessionView::mouseUp (const juce::MouseEvent& e)
{
    if (e.getPosition().y > headH || e.mouseWasDraggedSinceMouseDown()) return;
    const int sceneW = getWidth() < 420 ? 44 : 58;
    const int colW = (getWidth() - sceneW - kTracks * theme::gap) / kTracks;
    const int t = e.getPosition().x / (colW + theme::gap);
    if (t >= 0 && t < kTracks) { ctl.stopTrack (t); ctl.notify(); }
}

// =================================================================== the step grid / piano roll
StepGrid::StepGrid (Controller& c, std::function<void (int, int)> sheet) : ctl (c), onSheet (std::move (sheet))
{
    for (auto* b : { &lanes1, &lanes2, &steps1, &steps2, &lengthButton })
    {
        addAndMakeVisible (b);
        b->setClickingTogglesState (false);
    }
    // the drums: lanes 1-8 / 9-16; the piano roll: its window of rows down / up an octave of the scale
    lanes1.onClick = [this]
    {
        if (isDrum (ctl.sel)) lanePage = 0;
        else rowBase = juce::jmax (0, rowBase - ctl.key.def().size);
        repaint(); resized();
    };
    lanes2.onClick = [this]
    {
        if (isDrum (ctl.sel)) lanePage = 1;
        else rowBase = juce::jmin (kRows - shownRows(), rowBase + ctl.key.def().size);
        repaint(); resized();
    };
    steps1.onClick = [this] { stepPage = juce::jmax (0, juce::jmin (stepPage, stepPages() - 1) - 1); repaint(); resized(); };
    steps2.onClick = [this] { stepPage = juce::jmin (stepPages() - 1, stepPage + 1); repaint(); resized(); };
    lengthButton.onClick = [this] { lengthMenu(); };
    startTimerHz (30);
}
int StepGrid::length() const
{
    auto* c = const_cast<Controller&> (ctl).editClip();
    return c != nullptr ? (int) c->data.length : kBar;
}
void StepGrid::resized()
{
    auto head = getLocalBounds().removeFromTop (headH);
    const bool drum = isDrum (ctl.sel);
    const bool paged = stepPages() > 1;
    if (paged)
    {
        steps2.setBounds (head.removeFromRight (40)); head.removeFromRight (4);
        steps1.setBounds (head.removeFromRight (40)); head.removeFromRight (8);
    }
    steps1.setVisible (paged); steps2.setVisible (paged);
    steps1.setEnabled (firstStep() > 0);
    steps2.setEnabled (firstStep() + visibleSteps() < length());
    lanes2.setBounds (head.removeFromRight (drum ? 52 : 40)); head.removeFromRight (4);
    lanes1.setBounds (head.removeFromRight (drum ? 80 : 40)); head.removeFromRight (8);
    lanes1.setButtonText (drum ? "Sons 1-8" : "v");
    lanes2.setButtonText (drum ? "9-16" : "^");
    lanes1.setToggleState (drum && lanePage == 0, juce::dontSendNotification);
    lanes2.setToggleState (drum && lanePage == 1, juce::dontSendNotification);
    if (! drum) { lanes1.setEnabled (rowBase > 0); lanes2.setEnabled (rowBase + shownRows() < kRows); }
    else { lanes1.setEnabled (true); lanes2.setEnabled (true); }
    lengthButton.setButtonText (juce::String (length()) + " pas");
    lengthButton.setBounds (head.removeFromRight (narrow() ? 70 : 84));
    lengthButton.setVisible (ctl.editClip() != nullptr);
}
void StepGrid::lengthMenu()
{
    if (ctl.editClip() == nullptr) return;
    juce::PopupMenu m;
    m.addSectionHeader ("Longueur du clip");
    for (int len : { 4, 8, 12, 16, 24, 32, 48, 64 })
        m.addItem (len, juce::String (len) + " pas" + (len % kBar == 0 ? "  (" + juce::String (len / kBar) + " mes.)" : juce::String()),
                   true, len == length());
    m.addSeparator();
    m.addItem (1000, juce::String::fromUTF8 ("Doubler (le clip joué deux fois)"), length() * 2 <= kMaxSteps);
    m.addItem (1001, "Quantifier");
    const int t = ctl.sel, slot = ctl.editSlot[(size_t) t];
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lengthButton),
                     [this, t, slot] (int r)
                     {
                         if (r >= 1 && r <= kMaxSteps) ctl.setLength (t, slot, r);
                         if (r == 1000) ctl.doubleClip (t, slot);
                         if (r == 1001) ctl.quantizeClip (t, slot);
                         if (r > 0) { stepPage = juce::jmin (stepPage, stepPages() - 1); resized(); repaint(); }
                     });
}
std::vector<int> StepGrid::rows() const
{
    std::vector<int> r;
    if (isDrum (ctl.sel)) for (int i = 0; i < 8; ++i) r.push_back (lanePage * 8 + i);
    else
    {
        const int base = juce::jlimit (0, kRows - shownRows(), rowBase);
        for (int i = shownRows() - 1; i >= 0; --i) r.push_back (base + i);
    }
    return r;
}
bool StepGrid::cellAt (juce::Point<int> p, int& row, int& step) const
{
    auto a = gridArea();
    const auto rs = rows();
    if (! a.contains (p) || p.x < a.getX() + labelW()) return false;
    const float cw = (float) (a.getWidth() - labelW()) / (float) visibleSteps(), rh = (float) a.getHeight() / (float) rs.size();
    const int ci = (int) ((float) (p.x - a.getX() - labelW()) / cw), ri = (int) ((float) (p.y - a.getY()) / rh);
    if (ci < 0 || ci >= visibleSteps() || ri < 0 || ri >= (int) rs.size()) return false;
    row = rs[(size_t) ri];
    step = firstStep() + ci;
    return step < length();
}
int StepGrid::columnAt (int x) const
{
    auto a = gridArea();
    const float cw = (float) (a.getWidth() - labelW()) / (float) visibleSteps();
    return juce::jmin (length() - 1, firstStep() + juce::jlimit (0, visibleSteps() - 1, (int) std::floor ((float) (x - a.getX() - labelW()) / cw)));
}
int StepGrid::labelAt (juce::Point<int> p) const
{
    auto a = gridArea();
    const auto rs = rows();
    if (! a.contains (p) || p.x >= a.getX() + labelW()) return -1;
    const int ri = (int) ((float) (p.y - a.getY()) / ((float) a.getHeight() / (float) rs.size()));
    return ri >= 0 && ri < (int) rs.size() ? rs[(size_t) ri] : -1;
}
void StepGrid::preview (int row)
{
    if (isDrum (ctl.sel)) { ctl.audio.drumHit (row, 100); return; }
    const int t = ctl.sel, note = ctl.note (t, row);
    ctl.audio.note (t, note, true);
    juce::Timer::callAfterDelay (220, [a = &ctl.audio, t, note] { a->note (t, note, false); });
}
void StepGrid::paint (juce::Graphics& g)
{
    auto* clip = ctl.editClip();
    auto head = getLocalBounds().removeFromTop (headH);
    g.setColour (theme::dim);
    g.setFont (font (13.0f));
    juce::String title = juce::String (kTrackIds[ctl.sel]) + juce::String::fromUTF8 (" · ") + (clip ? clip->name : juce::String ("case vide"))
                         + (clip && ctl.editingPlaying() ? "  >" : "");
    if (clip && stepPages() > 1)
        title << "  " << (firstStep() + 1) << "-" << juce::jmin (length(), firstStep() + visibleSteps());
    const int right = (lengthButton.isVisible() ? lengthButton : lanes1.isVisible() ? lanes1 : steps1).getX() - 6;
    g.drawText (title, head.withRight (juce::jmax (head.getX(), right)), juce::Justification::centredLeft, true);
    auto a = gridArea();
    if (clip == nullptr)
    {
        g.drawText (juce::String::fromUTF8 ("Pas de clip ici : touche une case vide de la Session pour en créer un."), a,
                    juce::Justification::centred, true);
        return;
    }
    const auto& d = clip->data;
    const auto rs = rows();
    const float cw = (float) (a.getWidth() - labelW()) / (float) visibleSteps(), rh = (float) a.getHeight() / (float) rs.size();
    const auto col = theme::track (ctl.sel);
    const int ph = ctl.playing && ctl.editingPlaying() ? ctl.audio.position[ctl.sel].load() : -1;
    for (size_t i = 0; i < rs.size(); ++i)
    {
        const int row = rs[i];
        auto lab = juce::Rectangle<float> ((float) a.getX(), (float) a.getY() + (float) i * rh, (float) (labelW() - theme::gap), rh - (float) theme::gap);
        const bool selLane = d.drum && row == ctl.lane;
        g.setColour (selLane ? theme::cellBeat : theme::panel);
        g.fillRoundedRectangle (lab, theme::radius);
        if (selLane) { g.setColour (col); g.fillRect (lab.withWidth (3.0f)); }
        g.setColour (selLane ? col : (! d.drum && ctl.key.isRoot (row) ? theme::fg : theme::dim));
        g.setFont (font (d.drum ? 13.0f : 11.5f, d.drum));
        g.drawText (d.drum ? juce::String (kLaneNames[row]) : noteName (ctl.note (ctl.sel, row)),
                    lab.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
        for (int v = 0; v < visibleSteps(); ++v)
        {
            const int s = firstStep() + v;
            auto r = juce::Rectangle<float> ((float) (a.getX() + labelW()) + (float) v * cw, (float) a.getY() + (float) i * rh, cw - (float) theme::gap, rh - (float) theme::gap);
            if (s >= d.length) { g.setColour (theme::panel.withAlpha (0.5f)); g.fillRoundedRectangle (r, 2.0f); continue; }
            // a synth note held over several steps is drawn as one bar: its first step carries the marks
            const int start = d.drum ? (d.stepOn (row, s) ? s : -1) : d.noteStart (row, s);
            const auto& x = d.drum ? d.lanes[(size_t) row][(size_t) s] : d.notes[(size_t) juce::jmax (0, start)];
            const bool on = start >= 0, first = start == s;
            const juce::Colour beat = s % 4 == 0 ? theme::cellBeat : theme::cell;
            const juce::Colour empty = ! d.drum && ctl.key.isRoot (row) ? beat.interpolatedWith (col, 0.1f) : beat;
            g.setColour (on ? mixed (col, 0.35f + 0.65f * x.vel / 127.0f) : empty);
            g.fillRoundedRectangle (r, 2.0f);
            if (on && ! first && v > 0) g.fillRect (r.withX (r.getX() - (float) theme::gap - 2.0f).withWidth ((float) theme::gap + 4.0f));
            if (on && ! first && v == 0) { g.setColour (theme::ink.withAlpha (0.5f)); g.drawText ("<", r, juce::Justification::centredLeft); }
            if (first)
            {
                if (d.hasLocks (s)) { g.setColour (theme::ink); g.fillEllipse (r.getRight() - 9, r.getY() + 3, 6, 6); }
                if (x.prob < 100 || x.cond != Cond::always)
                {
                    juce::Path corner;
                    corner.addTriangle (r.getX() + 3, r.getY() + 3, r.getX() + 10, r.getY() + 3, r.getX() + 3, r.getY() + 10);
                    g.setColour (theme::ink.withAlpha (0.75f));
                    g.fillPath (corner);
                }
                juce::String tag;
                if (x.ratchet > 1) tag << "x" << (int) x.ratchet;
                if (x.prob < 100) tag << (tag.isEmpty() ? "" : " ") << (int) x.prob << "%";
                if (x.cond != Cond::always) tag << (tag.isEmpty() ? "" : " ") << condLabel (x.cond);
                if (tag.isNotEmpty() && rh > 22)
                {
                    g.setColour (theme::ink);
                    g.setFont (font (10.0f));
                    g.drawText (tag, r.reduced (3.0f, 1.0f), juce::Justification::bottomLeft, true);
                }
            }
            for (auto& [_, pt] : pointers)
                if ((pt.mode == Pointer::pressing || pt.mode == Pointer::stretching) && pt.row == row && pt.step == s)
                {
                    g.setColour (theme::fg);
                    g.drawRoundedRectangle (r.expanded (1.0f), 2.0f, 2.0f);
                }
            if (s == ph) { g.setColour (theme::fg); g.drawRoundedRectangle (r.reduced (1.0f), 2.0f, 2.0f); }
        }
    }
}
void StepGrid::timerCallback()
{
    const double t = now();
    for (auto& [src, p] : pointers)
        if (p.mode == Pointer::pressing && ! p.sheet && ! ctl.lockedSinceHold && t - p.time > kLongPress)
        {
            p.sheet = true;
            auto it = std::find (ctl.held.begin(), ctl.held.end(), std::make_pair (p.row, p.step));
            if (it != ctl.held.end()) ctl.held.erase (it);
            onSheet (p.row, p.step);
            break;
        }
    repaint();
}
void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int row, step;
    if (const int lab = labelAt (e.getPosition()); lab >= 0)
    {
        if (isDrum (ctl.sel)) ctl.lane = lab;
        preview (lab);
        ctl.notify();
        return;
    }
    if (! cellAt (e.getPosition(), row, step) || ctl.editClip() == nullptr) return;
    Pointer p { row, step, e.getPosition() };
    p.time = now();
    if (const int start = ctl.noteAt (row, step); start >= 0) p.step = start;   // (piano roll: on a long note, its start)
    p.created = ! ctl.editClip()->data.stepOn (row, p.step);
    if (p.created) { ctl.toggleStep (row, step, true); preview (row); }
    if (auto* x = ctl.step (row, p.step)) p.vel0 = x->vel;
    if (isDrum (ctl.sel)) ctl.lane = row;
    if (ctl.held.empty()) ctl.lockedSinceHold = false;
    ctl.held.push_back ({ row, p.step });
    pointers[e.source.getIndex()] = p;
    ctl.notify();
}
void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    auto it = pointers.find (e.source.getIndex());
    if (it == pointers.end()) return;
    auto& p = it->second;
    const auto d = e.getPosition() - p.at;
    if (p.mode == Pointer::pressing)
    {
        if (p.sheet || d.getDistanceFromOrigin() < 10) return;
        auto h = std::find (ctl.held.begin(), ctl.held.end(), std::make_pair (p.row, p.step));
        if (h != ctl.held.end()) ctl.held.erase (h);
        if (isDrum (ctl.sel) && ! p.created && std::abs (d.y) > std::abs (d.x)) p.mode = Pointer::velocity;
        else if (! isDrum (ctl.sel)) p.mode = Pointer::stretching;   // piano roll: a drag sets the note's length
        else
        {
            p.mode = Pointer::painting;
            p.value = p.created;
            if (! p.created) ctl.toggleStep (p.row, p.step, false);
        }
    }
    if (p.mode == Pointer::velocity)
        if (auto* x = ctl.step (p.row, p.step))
        {
            ctl.remember ("velocity");
            x->vel = (uint8_t) juce::jlimit (10, 127, p.vel0 - d.y * 127 / 150);
            ctl.stepChanged();
        }
    if (p.mode == Pointer::stretching)
        ctl.setNoteLength (p.row, p.step, columnAt (e.getPosition().x) - p.step + 1);
    if (p.mode == Pointer::painting)
    {
        int row, step;
        if (cellAt (e.getPosition(), row, step) && row == p.row
            && ctl.editClip()->data.stepOn (row, step) != p.value)
            ctl.toggleStep (row, step, p.value);
    }
}
void StepGrid::mouseUp (const juce::MouseEvent& e)
{
    auto it = pointers.find (e.source.getIndex());
    if (it == pointers.end()) return;
    auto p = it->second;
    pointers.erase (it);
    auto h = std::find (ctl.held.begin(), ctl.held.end(), std::make_pair (p.row, p.step));
    if (h != ctl.held.end()) ctl.held.erase (h);
    if (p.mode == Pointer::pressing && ! p.sheet && ! p.created && ! ctl.lockedSinceHold)
        ctl.toggleStep (p.row, p.step, false);
    ctl.notify();
}

// =================================================================== pads
juce::Rectangle<int> PadsView::headButton (int i) const
{
    if (isDrum (ctl.sel)) return { i * 76, 0, 72, 30 };
    if (i < 2) return { i * 56, 0, 52, 30 };          // Oct -, Oct +
    if (i == 2) return { 120, 0, 84, 30 };            // Accords
    return { 212, 0, juce::jmin (150, getWidth() - 212), 30 };   // the key
}
int PadsView::padAt (juce::Point<int> p) const
{
    auto a = padsArea();
    if (! a.contains (p)) return -1;
    const int c = (p.x - a.getX()) * cols() / a.getWidth(), r = (p.y - a.getY()) * 4 / a.getHeight();
    return juce::jlimit (0, cols() * 4 - 1, r * cols() + c);
}
int PadsView::padRow (int pad) const
{
    // from the bottom left: a column up the scale, a row up a fourth (three degrees of a 7-note scale)
    const int size = ctl.key.def().size, rowStep = (size * 3 + 3) / 7;
    const int col = pad % cols(), row = 3 - pad / cols();
    const int degree = octave * size + col + row * rowStep;
    return degree < kRows ? degree : -1;
}
std::vector<int> PadsView::padRows (int pad) const
{
    const int r = padRow (pad);
    if (r < 0) return {};
    std::vector<int> rows { r };
    if (chords)                                       // the scale's triad on it: degrees +2 and +4
        for (int k : { 2, 4 }) if (r + k < kRows) rows.push_back (r + k);
    return rows;
}
void PadsView::paint (juce::Graphics& g)
{
    const bool drum = isDrum (ctl.sel);
    const auto col = theme::track (ctl.sel);
    auto button = [&g] (juce::Rectangle<int> r, const juce::String& text, bool on)
    {
        g.setColour (on ? theme::fg : theme::panel);
        g.fillRoundedRectangle (r.toFloat(), theme::radius);
        g.setColour (on ? theme::ink : theme::fg);
        g.setFont (font (12.0f, true));
        g.drawText (text, r, juce::Justification::centred, true);
    };
    if (drum)
    {
        button (headButton (0), "QFG", qfg);
        button (headButton (1), "MPC", ! qfg);
    }
    else
    {
        button (headButton (0), "Oct -", false);
        button (headButton (1), "Oct +", false);
        button (headButton (2), "Accords", chords);
        button (headButton (3), pitchName (ctl.key.root) + " " + juce::String::fromUTF8 (ctl.key.def().name)
                                    + juce::String::fromUTF8 (" ▾"), false);
    }
    auto a = padsArea().toFloat();
    const int nc = cols();
    const float w = a.getWidth() / (float) nc, h = a.getHeight() / 4;
    for (int i = 0; i < nc * 4; ++i)
    {
        auto r = juce::Rectangle<float> (a.getX() + (float) (i % nc) * w, a.getY() + (float) (i / nc) * h, w, h).reduced (3.0f);
        const int row = drum ? -1 : padRow (i);
        const bool root = ! drum && row >= 0 && ctl.key.isRoot (row);
        g.setColour (drum || row >= 0 ? (root ? mixed (col, 0.28f, theme::panel) : theme::panel) : theme::panel.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 6.0f);
        for (auto& [_, d] : down)
            if (d.pad == i && d.track == ctl.sel)
            {
                g.setColour (col);
                g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * (1.0f - d.vel)), 6.0f);
            }
        if (drum && padSound (i) == ctl.lane) { g.setColour (col); g.drawRoundedRectangle (r, 6.0f, 2.0f); }
        if (! drum && row < 0) continue;
        g.setColour (theme::fg);
        g.setFont (font (13.0f, true));
        g.drawText (drum ? juce::String (kLaneNames[padSound (i)]) : noteName (ctl.note (ctl.sel, row)),
                    r.reduced (8.0f, 6.0f), juce::Justification::bottomLeft, true);
    }
}
void PadsView::mouseDown (const juce::MouseEvent& e)
{
    const bool drum = isDrum (ctl.sel);
    for (int i = 0; i < (drum ? 2 : 4); ++i)
        if (headButton (i).contains (e.getPosition()))
        {
            if (drum) qfg = i == 0;
            else if (i == 0) octave = juce::jmax (0, octave - 1);
            else if (i == 1) octave = juce::jmin (2, octave + 1);
            else if (i == 2) chords = ! chords;
            else if (onKey) onKey();
            repaint();
            return;
        }
    const int pad = padAt (e.getPosition());
    if (pad < 0) return;
    auto a = padsArea();
    const float cell = (float) a.getHeight() / 4.0f, y = (float) (e.getPosition().y - a.getY()) - (float) (pad / cols()) * cell;
    const float vel = juce::jlimit (0.25f, 1.0f, 1.0f - y / cell * 0.75f);   // higher on the pad, harder
    const int v = (int) (vel * 127);
    Down d { pad, vel, {}, ctl.sel };
    if (drum)
    {
        const int snd = padSound (pad);
        ctl.lane = snd;
        ctl.audio.drumHit (snd, v);
        ctl.recordHit (snd, v);                       // (REC on and playing: into the clip that plays)
    }
    else
    {
        for (int row : padRows (pad))
        {
            d.notes.push_back (ctl.note (ctl.sel, row));
            ctl.audio.note (ctl.sel, d.notes.back(), true);
            ctl.recordHit (row, v);
        }
        if (d.notes.empty()) return;
    }
    down[e.source.getIndex()] = d;
    ctl.notify();
}
void PadsView::mouseUp (const juce::MouseEvent& e)
{
    auto it = down.find (e.source.getIndex());
    if (it == down.end()) return;
    for (int n : it->second.notes) ctl.audio.note (it->second.track, n, false);
    down.erase (it);
    repaint();
}

// =================================================================== icons
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

// =================================================================== knobs
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
    juce::String name = juce::String (kTrackIds[ctl.sel]) + juce::String::fromUTF8 (" · ")
                        + (isDrum (ctl.sel) ? se_kit_name (snd.preset) : se_preset_name (snd.engine, snd.preset));
    if (! ctl.held.empty()) name << juce::String::fromUTF8 (" · pas ") << (ctl.held.front().second + 1);
    else name << juce::String::fromUTF8 ("  ▾");
    g.drawText (name, title.withTrimmedLeft (10), juce::Justification::centredLeft, true);

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
    if (titleRect().contains (e.getPosition()) && ctl.held.empty()) { if (onSound) onSound(); return; }
    const auto pages = ctl.pages();
    for (int i = 0; i < (int) pages.size(); ++i)
        if (pageRect (i).contains (e.getPosition())) { ctl.setPage (pages[(size_t) i]); return; }
    Drag d;
    d.k = knobAt (e.getPosition());
    d.y = (float) e.getPosition().y;
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
    const float dy = d.y - (float) e.getPosition().y;
    d.y = (float) e.getPosition().y;
    d.moved += std::abs (dy);
    if (d.moved < 3) return;
    const bool fine = drags.size() > 1;                  // a second finger on the strip: fine adjustment
    // a short range (a wave, a mode: a few values) moves slower: one value per 18 points or so
    const float span = (float) (def.max - def.min), travel = juce::jmax (fine ? 1000.0f : 180.0f, span * 18.0f);
    d.value += dy * span / travel;
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

// =================================================================== the step sheet
StepSheet::StepSheet (Controller& c, int r, int s, std::function<void()> close)
    : ctl (c), row (r), step (s), onClose (std::move (close))
{
    auto make = [this] (juce::OwnedArray<juce::TextButton>& arr, juce::StringArray labels, std::function<void (int)> f)
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = arr.add (new juce::TextButton (labels[i]));
            b->onClick = [f, i, this] { ctl.remember ("sheet"); f (i); sync(); ctl.stepChanged(); };
            addAndMakeVisible (b);
        }
    };
    make (prob, { "100%", "75%", "50%", "25%" }, [this] (int i) { if (auto* x = ctl.step (row, step)) x->prob = (uint8_t) (100 - i * 25); });
    make (cond, { "Toujours", "1:2", "2:2", "1:4", "Fill", "Sans fill" }, [this] (int i) { if (auto* x = ctl.step (row, step)) x->cond = (Cond) i; });
    make (ratchet, { "x1", "x2", "x3", "x4" }, [this] (int i) { if (auto* x = ctl.step (row, step)) x->ratchet = (uint8_t) (i + 1); });
    early.onClick = [this] { ctl.remember ("sheet"); if (auto* x = ctl.step (row, step)) x->nudge = (int8_t) juce::jmax (-3, x->nudge - 1); sync(); ctl.stepChanged(); };
    late.onClick = [this] { ctl.remember ("sheet"); if (auto* x = ctl.step (row, step)) x->nudge = (int8_t) juce::jmin (3, x->nudge + 1); sync(); ctl.stepChanged(); };
    clearLocks.onClick = [this] { ctl.remember ("sheet"); if (auto* clip = ctl.editClip()) clip->data.clearLock (step, -1); ctl.stepChanged(); };
    del.onClick = [this]
    {
        if (auto* clip = ctl.editClip())
        {
            ctl.remember ("sheet");
            if (clip->data.drum) clip->data.lanes[(size_t) row][(size_t) step] = {};
            else { clip->data.notes[(size_t) step] = {}; clip->data.noteLen[(size_t) step].fill (0); }
            ctl.stepChanged();
        }
        onClose();
    };
    ok.onClick = [this] { onClose(); };
    vel.setSliderStyle (juce::Slider::LinearHorizontal);
    vel.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 24);
    vel.setRange (1, 127, 1);
    vel.onValueChange = [this] { if (auto* x = ctl.step (row, step)) { ctl.remember ("sheet"); x->vel = (uint8_t) vel.getValue(); ctl.stepChanged(); } };
    for (auto* b : { &early, &late, &clearLocks, &del, &ok }) addAndMakeVisible (b);
    addAndMakeVisible (vel);
    sync();
}
void StepSheet::sync()
{
    auto* x = ctl.step (row, step);
    if (x == nullptr) return;
    for (int i = 0; i < prob.size(); ++i) prob[i]->setToggleState (x->prob == 100 - i * 25, juce::dontSendNotification);
    for (int i = 0; i < cond.size(); ++i) cond[i]->setToggleState ((int) x->cond == i, juce::dontSendNotification);
    for (int i = 0; i < ratchet.size(); ++i) ratchet[i]->setToggleState (x->ratchet == i + 1, juce::dontSendNotification);
    vel.setValue (x->vel, juce::dontSendNotification);
    repaint();
}
juce::Rectangle<int> StepSheet::panel() const
{
    const int w = juce::jmin (getWidth(), 640), h = juce::jmin (getHeight(), 360);
    return { (getWidth() - w) / 2, getHeight() - h, w, h };
}
void StepSheet::resized()
{
    auto p = panel().reduced (16);
    p.removeFromTop (34);
    auto rowOf = [&p] { auto r = p.removeFromTop (40); p.removeFromTop (8); return r.withTrimmedLeft (100); };
    auto lay = [] (juce::Rectangle<int> r, juce::Array<juce::Component*> cs)
    {
        const int w = juce::jmin (90, r.getWidth() / juce::jmax (1, cs.size()));
        for (auto* c : cs) { c->setBounds (r.removeFromLeft (w - 4)); r.removeFromLeft (4); }
    };
    lay (rowOf(), { prob[0], prob[1], prob[2], prob[3] });
    lay (rowOf(), { cond[0], cond[1], cond[2], cond[3], cond[4], cond[5] });
    lay (rowOf(), { ratchet[0], ratchet[1], ratchet[2], ratchet[3] });
    lay (rowOf(), { &early, &late, &clearLocks });
    vel.setBounds (rowOf());
    auto bottom = p.removeFromBottom (44);
    ok.setBounds (bottom.removeFromRight (90)); bottom.removeFromRight (8);
    del.setBounds (bottom.removeFromRight (120));
}
void StepSheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    auto p = panel();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (p.toFloat(), 10.0f);
    auto* x = ctl.step (row, step);
    auto title = p.reduced (16).removeFromTop (28);
    g.setColour (theme::fg);
    g.setFont (font (19.0f, true));
    g.drawText ((isDrum (ctl.sel) ? juce::String (kLaneNames[row]) : juce::String (kTrackNames[ctl.sel])) + juce::String::fromUTF8 ("  ·  pas ")
                    + juce::String (step + 1) + (x && x->nudge ? juce::String::fromUTF8 ("  (décalage ") + juce::String (x->nudge) + "/6)" : ""),
                title, juce::Justification::centredLeft);
    g.setColour (theme::dim);
    g.setFont (font (12.0f, true));
    auto labels = p.reduced (16).withTrimmedTop (34);
    for (auto* l : { "Probabilité", "Condition", "Ratchet", "Décalage", "Vélocité" })
    {
        g.drawText (juce::String::fromUTF8 (l), labels.removeFromTop (40).withWidth (96), juce::Justification::centredLeft);
        labels.removeFromTop (8);
    }
}
void StepSheet::mouseUp (const juce::MouseEvent& e)
{
    if (! panel().contains (e.getPosition())) onClose();
}

// =================================================================== the sound sheet
SoundSheet::SoundSheet (Controller& c, std::function<void()> close) : ctl (c), onClose (std::move (close))
{
    engineView.setViewedComponent (&engineRow, false);
    engineView.setScrollBarsShown (false, false, false, true);
    engineView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    itemView.setViewedComponent (&itemGrid, false);
    itemView.setScrollBarsShown (false, false, true, false);
    itemView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (itemView);
    addAndMakeVisible (ok);
    ok.onClick = [this] { onClose(); };
    if (! isDrum (ctl.sel))
    {
        addAndMakeVisible (engineView);
        for (int e = 0; e < se_engine_count(); ++e)
        {
            auto* b = engineButtons.add (new juce::TextButton (se_engine_name (e)));
            b->onClick = [this, e] { showEngine (e); };
            engineRow.addAndMakeVisible (b);
        }
        showEngine (ctl.sounds[(size_t) ctl.sel].engine);
    }
    else
    {
        for (int k = 0; k < se_kit_count(); ++k)
        {
            const juce::String name (se_kit_name (k));
            if (name.startsWith ("USR")) continue;    // the user sample slots: nothing loaded in the app yet
            auto* b = itemButtons.add (new juce::TextButton (name));
            itemIds.add (k);
            b->onClick = [this, k] { ctl.setSound (0, -1, k); sync(); audition(); };
            itemGrid.addAndMakeVisible (b);
        }
        sync();
    }
}
void SoundSheet::showEngine (int engine)
{
    engineShown = engine;
    itemButtons.clear();
    itemIds.clear();
    for (int p = 0; p < se_preset_count (engine); ++p)
    {
        auto* b = itemButtons.add (new juce::TextButton (se_preset_name (engine, p)));
        itemIds.add (p);
        b->onClick = [this, engine, p] { ctl.setSound (ctl.sel, engine, p); sync(); audition(); };
        itemGrid.addAndMakeVisible (b);
    }
    sync();
    resized();
}
void SoundSheet::sync()
{
    const auto& s = ctl.sounds[(size_t) ctl.sel];
    for (int e = 0; e < engineButtons.size(); ++e)
        engineButtons[e]->setToggleState (e == engineShown, juce::dontSendNotification);
    const bool here = isDrum (ctl.sel) || s.engine == engineShown;
    for (int i = 0; i < itemButtons.size(); ++i)
        itemButtons[i]->setToggleState (here && itemIds[i] == s.preset, juce::dontSendNotification);
    repaint();
}
void SoundSheet::audition()
{
    auto* a = &ctl.audio;
    if (isDrum (ctl.sel))
    {
        a->drumHit (0, 110);
        juce::Timer::callAfterDelay (220, [a] { a->drumHit (4, 90); });
        juce::Timer::callAfterDelay (440, [a] { a->drumHit (3, 110); });
        return;
    }
    const int t = ctl.sel, note = ctl.note (t, 7);
    a->note (t, note, true);
    juce::Timer::callAfterDelay (450, [a, t, note] { a->note (t, note, false); });
}
juce::Rectangle<int> SoundSheet::panel() const
{
    const int w = juce::jmin (getWidth(), 760), h = juce::jmin (getHeight() - 24, 560);
    return { (getWidth() - w) / 2, getHeight() - h, w, h };
}
void SoundSheet::resized()
{
    auto p = panel().reduced (16);
    p.removeFromTop (34);
    auto bottom = p.removeFromBottom (44);
    ok.setBounds (bottom.removeFromRight (90));
    p.removeFromBottom (8);
    if (! engineButtons.isEmpty())
    {
        auto row = p.removeFromTop (40);
        p.removeFromTop (4);
        about = p.removeFromTop (34);                 // what the engine is (paint)
        p.removeFromTop (6);
        engineView.setBounds (row);
        const int w = 100;
        engineRow.setSize (w * engineButtons.size(), row.getHeight());
        for (int e = 0; e < engineButtons.size(); ++e) engineButtons[e]->setBounds (e * w, 0, w - 6, row.getHeight());
    }
    itemView.setBounds (p);
    const int cols = juce::jmax (2, p.getWidth() / 150), cw = p.getWidth() / cols, rh = 46;
    itemGrid.setSize (p.getWidth(), ((itemButtons.size() + cols - 1) / cols) * rh);
    for (int i = 0; i < itemButtons.size(); ++i)
        itemButtons[i]->setBounds ((i % cols) * cw, (i / cols) * rh, cw - 6, rh - 6);
}
void SoundSheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    auto p = panel();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (p.toFloat(), 10.0f);
    g.setColour (theme::track (ctl.sel));
    auto title = p.reduced (16).removeFromTop (28);
    g.fillRect (title.getX(), title.getCentreY() - 8, 3, 16);
    g.setColour (theme::fg);
    g.setFont (font (19.0f, true));
    g.drawText (juce::String::fromUTF8 ("Son · ") + kTrackIds[ctl.sel] + " " + kTrackNames[ctl.sel]
                    + (isDrum (ctl.sel) ? juce::String::fromUTF8 ("  ·  kit") : juce::String()),
                title.withTrimmedLeft (12), juce::Justification::centredLeft);
    if (! isDrum (ctl.sel) && ! about.isEmpty())
    {
        g.setColour (theme::dim);
        g.setFont (font (12.5f));
        g.drawFittedText (engineAbout (se_engine_name (engineShown)), about, juce::Justification::centredLeft, 2);
    }
}
void SoundSheet::mouseUp (const juce::MouseEvent& e)
{
    if (! panel().contains (e.getPosition())) onClose();
}

// what each engine is, in a line (the sound sheet), by its name
juce::String engineAbout (const juce::String& name)
{
    static const std::pair<const char*, const char*> about[] = {
        { "ANALOG", "Soustractif : deux oscillateurs (chacun sa forme), sous-oscillateur, sync, filtre 24 dB à la Moog et "
                    "son enveloppe. Basses 808, acid, leads, nappes." },
        { "DIGITAL", "FM à 4 opérateurs : pianos électriques, cloches, claviers, basses métalliques." },
        { "PHASE", "Distorsion de phase, à la Casio CZ : des sons numériques ronds ou mordants." },
        { "LOFI", "Puce 8 bits : carré, triangle, bruit, onde 4 bits. Les sons des consoles." },
        { "SAMPLE", "Des instruments enregistrés (piano, contrebasse, cuivres, cordes...) joués au clavier." },
        { "VOICE", "Des voyelles chantées (formants) : chœurs, talkbox, robot." },
        { "TRIO", "Trois oscillateurs, ring mod et sync, un filtre : les puces des ordinateurs 8 bits, les stabs rave." },
        { "WHEEL", "Orgue à roues phoniques : tirettes, percussion, cabine tournante." },
        { "GRAIN", "Granulaire sur les sons enregistrés : nuages, textures, nappes figées." },
        { "FM6", "FM à 6 opérateurs, à la DX7 (le cœur de Dexed) : les classiques des années 80." },
        { "PHYS", "Modèles physiques (Rings) : cordes pincées ou frottées, cloches, percussions." },
        { "NOISE", "Des bruits filtrés : vent, pluie, vinyle, montées, effets." },
        { "PLAITS", "Mutable Instruments Plaits : 24 modèles (analogique, FM, wavetable, accords, voix, cordes, percussions) "
                    "réglés par 4 macros : MODEL, HARM, TIMB, MORPH." },
    };
    for (auto& [n, text] : about)
        if (name == n) return juce::String::fromUTF8 (text);
    return {};
}

// =================================================================== the settings
SettingsSheet::SettingsSheet (Controller& c, std::function<void()> close, std::function<juce::String()> info,
                              std::function<void()> nextBuffer)
    : ctl (c), onClose (std::move (close)), audioInfo (std::move (info))
{
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (false, false, true, false);
    view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (view);
    addAndMakeVisible (ok);
    ok.onClick = [this] { onClose(); };
    ok.setToggleState (true, juce::dontSendNotification);
    for (auto* text : { "Tonalité", "Gamme", "Swing", "Jeu", "Audio" })
    {
        auto* l = labels.add (new Label());
        l->text = juce::String::fromUTF8 (text);
        content.addAndMakeVisible (l);
    }
    for (int k = 0; k < 12; ++k)
    {
        auto* b = roots.add (new juce::TextButton (pitchName (k)));
        b->onClick = [this, k] { auto key = ctl.key; key.root = k; ctl.setKey (key); sync(); };
        content.addAndMakeVisible (b);
    }
    for (int i = 0; i < (int) scales().size(); ++i)
    {
        auto* b = scaleButtons.add (new juce::TextButton (juce::String::fromUTF8 (scales()[(size_t) i].name)));
        b->onClick = [this, i] { auto key = ctl.key; key.scale = i; ctl.setKey (key); sync(); };
        content.addAndMakeVisible (b);
    }
    swing.setSliderStyle (juce::Slider::LinearHorizontal);
    swing.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 28);
    swing.setRange (0, 100, 1);
    swing.textFromValueFunction = [] (double v) { return juce::String (50.0 + v / 4.0, 1) + " %"; };   // MPC's swing
    swing.onValueChange = [this] { ctl.setSwing ((int) swing.getValue()); };
    content.addAndMakeVisible (swing);
    metronome.onClick = [this] { auto p = ctl.prefs; p.metronome = ! p.metronome; ctl.setPrefs (p); sync(); };
    countIn.onClick = [this] { auto p = ctl.prefs; p.countIn = ! p.countIn; ctl.setPrefs (p); sync(); };
    quantize.onClick = [this] { auto p = ctl.prefs; p.quantizeRec = ! p.quantizeRec; ctl.setPrefs (p); sync(); };
    buffer.onClick = [this, nextBuffer] { nextBuffer(); sync(); };
    for (auto* b : { &metronome, &countIn, &quantize, &buffer }) content.addAndMakeVisible (b);
    sync();
}
void SettingsSheet::Label::paint (juce::Graphics& g)
{
    g.setColour (theme::dim);
    g.setFont (font (12.0f, true));
    g.drawText (text, getLocalBounds(), juce::Justification::bottomLeft);
}
void SettingsSheet::sync()
{
    for (int k = 0; k < roots.size(); ++k) roots[k]->setToggleState (k == ctl.key.root, juce::dontSendNotification);
    for (int i = 0; i < scaleButtons.size(); ++i) scaleButtons[i]->setToggleState (i == ctl.key.scale, juce::dontSendNotification);
    swing.setValue (ctl.swing, juce::dontSendNotification);
    metronome.setToggleState (ctl.prefs.metronome, juce::dontSendNotification);
    countIn.setToggleState (ctl.prefs.countIn, juce::dontSendNotification);
    quantize.setToggleState (ctl.prefs.quantizeRec, juce::dontSendNotification);
    buffer.setButtonText (audioInfo ? audioInfo() : juce::String());
    repaint();
}
juce::Rectangle<int> SettingsSheet::panel() const
{
    const int w = juce::jmin (getWidth(), 680), h = juce::jmin (getHeight() - 24, 600);
    return { (getWidth() - w) / 2, getHeight() - h, w, h };
}
void SettingsSheet::resized()
{
    auto p = panel().reduced (16);
    p.removeFromTop (34);
    auto bottom = p.removeFromBottom (44);
    ok.setBounds (bottom.removeFromRight (90));
    p.removeFromBottom (8);
    view.setBounds (p);
    // the content, as tall as it needs: a label, then its buttons in rows
    const int w = p.getWidth(), bh = 40, g = 6;
    int y = 0;
    auto label = [&] (int i) { labels[i]->setBounds (0, y, w, 24); y += 28; };
    auto flow = [&] (juce::Array<juce::Component*> cs, int minW)
    {
        const int perRow = juce::jmax (1, juce::jmin (cs.size(), (w + g) / (minW + g)));
        const int bw = (w - (perRow - 1) * g) / perRow;
        for (int i = 0; i < cs.size(); ++i)
            cs[i]->setBounds ((i % perRow) * (bw + g), y + (i / perRow) * (bh + g), bw, bh);
        y += ((cs.size() + perRow - 1) / perRow) * (bh + g) + 8;
    };
    juce::Array<juce::Component*> r, sc;
    for (auto* b : roots) r.add (b);
    for (auto* b : scaleButtons) sc.add (b);
    label (0); flow (r, 44);
    label (1); flow (sc, 118);
    label (2); swing.setBounds (0, y, w, bh); y += bh + 8;
    label (3); flow ({ &metronome, &countIn, &quantize }, 180);
    label (4); flow ({ &buffer }, 200);
    content.setSize (w, y);
}
void SettingsSheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    auto p = panel();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (p.toFloat(), 10.0f);
    g.setColour (theme::fg);
    g.setFont (font (19.0f, true));
    g.drawText (juce::String::fromUTF8 ("Réglages"), p.reduced (16).removeFromTop (28), juce::Justification::centredLeft);
}
void SettingsSheet::mouseUp (const juce::MouseEvent& e)
{
    if (! panel().contains (e.getPosition())) onClose();
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
    g.drawText ("BPM", r.withTrimmedTop (r.getHeight() * 0.68f), juce::Justification::centredTop);
}
} // namespace sloop
