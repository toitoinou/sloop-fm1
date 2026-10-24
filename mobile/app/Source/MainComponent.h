// SPDX-License-Identifier: GPL-3.0-only
// The app's one screen, laid out as the mockup: in landscape a rail on the left (transport, tracks), the stage in
// the middle (Session, Grille, Pads), the device column on the right (FILL, the knobs); in portrait stacked.
#pragma once
#include "Views.h"

namespace sloop
{
class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTabNamed (const juce::String& name)
    {
        showTab (name == "grid" ? Tab::grid : name == "pads" ? Tab::pads : Tab::session);
    }

private:
    enum class Tab { session, grid, pads };
    void timerCallback() override;
    void showTab (Tab);
    void refresh();
    void openSheet (int row, int step);
    void tap();

    theme::LookAndFeel look;
    juce::AudioDeviceManager devices;
    AudioEngine audio;
    Controller ctl { audio };

    juce::TextButton playButton { ">" }, tapButton { "TAP" }, stopClips { "Stop clips" };
    HoldButton fillButton { "FILL" };
    BpmBox bpm { ctl };
    TrackRail rail { ctl };
    juce::TextButton tabSession { "Session" }, tabGrid { "Grille" }, tabPads { "Pads" };
    SessionView session { ctl, [this] (int t, int s) { ctl.editSlot[(size_t) t] = s; ctl.select (t); showTab (Tab::grid); } };
    StepGrid grid { ctl, [this] (int r, int s) { openSheet (r, s); } };
    PadsView pads { ctl };
    KnobStrip knobs { ctl };
    std::unique_ptr<StepSheet> sheet;
    Tab tab = Tab::session;
    std::vector<double> taps;
};
} // namespace sloop
