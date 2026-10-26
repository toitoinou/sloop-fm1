// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SettingsSheet.h"
#include "ui/Common.h"
#include "audio/Midi.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

SettingsSheet::SettingsSheet (Controller& c, std::function<void()> close, std::function<juce::String()> info,
                              std::function<void()> nextBuffer, std::function<juce::String()> midi)
    : ctl (c), onClose (std::move (close)), audioInfo (std::move (info)), midiInfo (std::move (midi))
{
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (false, false, true, false);
    view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (view);
    addAndMakeVisible (ok);
    ok.onClick = [this] { onClose(); };
    ok.setToggleState (true, juce::dontSendNotification);
    for (auto* text : { "Tonalité", "Gamme", "Swing", "Jeu", "Audio", "MIDI (notes sur la piste choisie, CC 21-24 / 70-73 : les boutons)",
                        "Ableton Link (le tempo, la mesure, Play / Stop avec les apps du même Wi-Fi)" })
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
    clockOut.onClick = [this] { auto p = ctl.prefs; p.midiClockOut = ! p.midiClockOut; ctl.setPrefs (p); sync(); };
    bluetooth.onClick = [] { MidiHub::pairBluetooth(); };
    midiInputs.onClick = [this] { sync(); };          // (a tap: the list again)
    for (auto* b : { &midiInputs, &clockOut, &bluetooth }) content.addAndMakeVisible (b);
   #if ! (JUCE_ANDROID || JUCE_IOS)
    bluetooth.setVisible (false);
   #endif
    link.onClick = [this] { auto p = ctl.prefs; p.link = ! p.link; ctl.setPrefs (p); sync(); };
    content.addAndMakeVisible (link);
    startTimerHz (2);
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
    midiInputs.setButtonText (midiInfo ? midiInfo() : juce::String());
    clockOut.setToggleState (ctl.prefs.midiClockOut, juce::dontSendNotification);
    link.setToggleState (ctl.prefs.link, juce::dontSendNotification);
    const int peers = ctl.prefs.link ? ctl.audio.link.peers() : 0;
    link.setButtonText (! ctl.prefs.link ? juce::String ("Link : non")
                        : peers == 0 ? juce::String::fromUTF8 ("Link : oui (aucune autre app)")
                        : "Link : oui (" + juce::String (peers) + (peers == 1 ? " app)" : " apps)"));
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
    label (5);
    juce::Array<juce::Component*> midiRow { &midiInputs, &clockOut };
    if (bluetooth.isVisible()) midiRow.add (&bluetooth);
    flow (midiRow, 180);
    label (6); flow ({ &link }, 200);
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
} // namespace sloop
