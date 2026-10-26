// SPDX-License-Identifier: GPL-3.0-only
// The step sheet (a long press on a step): probability, condition, ratchet, nudge, velocity, locks.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"

namespace sloop
{
// the long-press sheet of a step: probability, condition, ratchet, nudge, velocity, locks
class StepSheet : public juce::Component
{
public:
    StepSheet (Controller& c, int row, int step, std::function<void()> close);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void sync();
    juce::Rectangle<int> panel() const;
    Controller& ctl;
    int row, step;
    std::function<void()> onClose;
    juce::OwnedArray<juce::TextButton> prob, cond, ratchet;
    juce::TextButton early { "< avant" }, late { juce::String::fromUTF8 ("après >") }, clearLocks { "Effacer les locks" }, del { "Supprimer" },
        ok { "OK" };
    juce::Slider vel;
};
} // namespace sloop
