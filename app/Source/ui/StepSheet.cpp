// SPDX-License-Identifier: GPL-3.0-only
#include "ui/StepSheet.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

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
    g.drawText ((isDrum (ctl.sel) ? juce::String (kLaneNames[row]) : juce::String (trackName (ctl.sel))) + juce::String::fromUTF8 ("  ·  pas ")
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
} // namespace sloop
