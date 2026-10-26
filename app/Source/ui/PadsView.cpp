// SPDX-License-Identifier: GPL-3.0-only
#include "ui/PadsView.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

juce::Rectangle<int> PadsView::headButton (int i) const
{
    // on the right of every track: REPEAT and its rate
    if (i == kRepeat) return { getWidth() - 132, 0, 72, 30 };
    if (i == kRate) return { getWidth() - 56, 0, 56, 30 };
    if (isDrum (ctl.sel)) return { i * 66, 0, 62, 30 };
    if (i < 2) return { i * 50, 0, 46, 30 };          // Oct -, Oct +
    if (i == 2) return { 100, 0, 76, 30 };            // Accords
    return { 182, 0, juce::jmax (40, juce::jmin (150, getWidth() - 182 - 140)), 30 };   // the key
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
    button (headButton (kRepeat), "Repeat", ctl.repeatOn);
    button (headButton (kRate), repeatName (ctl.repeatRate), false);
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
    if (headButton (kRepeat).contains (e.getPosition())) { ctl.repeatOn = ! ctl.repeatOn; repaint(); return; }
    if (headButton (kRate).contains (e.getPosition())) { ctl.repeatRate = (ctl.repeatRate + 1) % 6; repaint(); return; }
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
    Down d { pad, vel, {}, {}, ctl.sel };
    if (drum)
    {
        const int snd = padSound (pad);
        ctl.lane = snd;
        ctl.audio.drumHit (snd, v);
        ctl.recordHit (snd, v);                       // (REC on and playing: into the clip that plays)
        if (ctl.repeatOn) { ctl.repeatPress (ctl.sel, snd, snd, v); d.repeats.push_back (snd); }
    }
    else
    {
        for (int row : padRows (pad))
        {
            d.notes.push_back (ctl.note (ctl.sel, row));
            d.rows.push_back (row);
            ctl.audio.note (ctl.sel, d.notes.back(), true, v);
            if (ctl.repeatOn) { ctl.repeatPress (ctl.sel, row, d.notes.back(), v); d.repeats.push_back (d.notes.back()); }
            else ctl.recordHit (row, v);                // (a repeat: its hits recorded step by step)
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
    for (int r : it->second.rows) ctl.recordRelease (it->second.track, r);   // (recording: the note's length)
    for (int id : it->second.repeats) ctl.repeatRelease (it->second.track, id);
    down.erase (it);
    repaint();
}
} // namespace sloop
