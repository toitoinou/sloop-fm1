// SPDX-License-Identifier: GPL-3.0-only
// The settings: the key, the swing, the metronome and count-in, the audio buffer, MIDI, Ableton Link.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"

namespace sloop
{
// the settings: the key and its scale, the swing; the app's metronome, count-in, recording quantize; the audio
class SettingsSheet : public juce::Component, private juce::Timer
{
public:
    SettingsSheet (Controller& c, std::function<void()> close, std::function<juce::String()> audioInfo,
                   std::function<void()> nextBuffer, std::function<juce::String()> midiInfo);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void sync();
    void timerCallback() override { sync(); }            // (Link: the apps found)
    juce::Rectangle<int> panel() const;
    Controller& ctl;
    std::function<void()> onClose;
    std::function<juce::String()> audioInfo, midiInfo;
    juce::Component content;
    juce::Viewport view;
    juce::OwnedArray<juce::TextButton> roots, scaleButtons;
    juce::Slider swing;
    juce::TextButton metronome { juce::String::fromUTF8 ("Métronome") }, countIn { juce::String::fromUTF8 ("Décompte 1 mesure") },
        quantize { juce::String::fromUTF8 ("Enregistrer sur la grille") }, buffer, ok { "OK" },
        midiInputs, bluetooth { juce::String::fromUTF8 ("Bluetooth MIDI…") }, clockOut { juce::String::fromUTF8 ("Horloge MIDI (sortie)") },
        link;
    struct Label : juce::Component
    {
        juce::String text;
        void paint (juce::Graphics&) override;
    };
    juce::OwnedArray<Label> labels;
};
} // namespace sloop
