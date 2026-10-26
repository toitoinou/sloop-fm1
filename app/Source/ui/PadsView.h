// SPDX-License-Identifier: GPL-3.0-only
// The pads: the drum sounds (QFG, MPC layouts) or a keyboard in fourths in the key, chords, octaves, note repeat;
// the velocity from where a pad is hit.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"
#include <map>

namespace sloop
{
// The pads. The drums: 4 x 4, as Quest for Groove lays them out (the kick and the snare doubled, for two hands),
// or the 16 sounds in order from the bottom left as an MPC. A synth: a keyboard in the key, in fourths as a Push
// or a Launchpad (a row up: three degrees of the scale up), only the scale's notes, the roots lit; CHORDS plays
// the triad of the scale on each pad
class PadsView : public juce::Component
{
public:
    explicit PadsView (Controller& c) : ctl (c) {}
    std::function<void()> onKey;                     // the key button tapped (the settings)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> padsArea() const { return getLocalBounds().withTrimmedTop (36); }
    juce::Rectangle<int> headButton (int i) const;   // drums: QFG, MPC; a synth: Oct -, Oct +, Accords, the key
    int cols() const { return isDrum (ctl.sel) ? 4 : getWidth() >= 520 ? 8 : 5; }
    int padAt (juce::Point<int>) const;
    int padSound (int pad) const
    {
        static constexpr int qfgLanes[16] = { 9, 14, 10, 11,      // low tom, mid tom (conga), high tom, crash
                                              4, 5, 4, 12,        // hat, open hat, hat, ride
                                              7, 2, 2, 7,         // rim (sidestick), snare, snare, rim
                                              3, 0, 0, 13 };      // clap, kick, kick, shaker
        return qfg ? qfgLanes[pad] : (3 - pad / 4) * 4 + pad % 4;
    }
    int padRow (int pad) const;                      // a synth pad's degree (piano roll row), -1 out of range
    std::vector<int> padRows (int pad) const;        // the rows it plays (a chord: three)
    Controller& ctl;
    bool qfg = true, chords = false;
    int octave = 0;
    struct Down { int pad; float vel; std::vector<int> notes, rows; int track; std::vector<int> repeats = {}; };
    static constexpr int kRepeat = 10, kRate = 11;   // the header's REPEAT and rate buttons (headButton)
    std::map<int, Down> down;                        // finger -> what it plays
};
} // namespace sloop
