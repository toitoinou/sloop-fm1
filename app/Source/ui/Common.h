// SPDX-License-Identifier: GPL-3.0-only
// What the screens share: gestures' timings, the drum lanes' names, small helpers. Touch first: every gesture
// follows one finger (MouseEvent::source), so several fingers can act at once (hold a step + turn a knob, hold a
// clip + tap others).
#pragma once
#include "model/Model.h"
#include "ui/Theme.h"

namespace sloop::ui
{
using theme::font;
constexpr double kLongPress = 480.0, kDoubleTap = 320.0;   // ms
constexpr int kMoveSlop = 12;                                // a finger moved more than this: a drag, not a tap
inline double now() { return juce::Time::getMillisecondCounterHiRes(); }
inline juce::Colour mixed (juce::Colour c, float amount, juce::Colour onto = theme::cell) { return onto.interpolatedWith (c, amount); }
inline const char* const kLaneNames[kLanes] = { "Kick", "Kick 2", "Snare", "Clap", "Hat", "Open hat", "Pedal", "Rim",
                                                "Snare 2", "Tom bas", "Tom haut", "Crash", "Ride", "Shaker", "Conga", "Cowbell" };
inline juce::String condLabel (Cond c)
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
} // namespace sloop::ui
