// SPDX-License-Identifier: GPL-3.0-only
// The track rail: the tracks shown (a tap selects one, a long press mutes it), their LEDs; "+" after them: a track
// more, the audio track, or the last one taken away.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"

namespace sloop
{
class TrackRail : public juce::Component, private juce::Timer
{
public:
    explicit TrackRail (Controller& c) : ctl (c) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> slot (int t) const;             // t == ctl.trackCount: the "+"
    int trackAt (juce::Point<int>) const;
    void tracksMenu();
    Controller& ctl;
    std::array<int, kTracks> seenHits {};
    std::array<float, kTracks> led {};
    int downTrack = -1;
    double downTime = 0;
    bool longDone = false;
};
} // namespace sloop
