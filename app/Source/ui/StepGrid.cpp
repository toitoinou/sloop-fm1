// SPDX-License-Identifier: GPL-3.0-only
#include "ui/StepGrid.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

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
    juce::String title = juce::String (trackId (ctl.sel)) + juce::String::fromUTF8 (" · ") + (clip ? clip->name : juce::String ("case vide"))
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
} // namespace sloop
