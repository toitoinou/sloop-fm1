// SPDX-License-Identifier: GPL-3.0-only
// Undo / redo: states as they were before each edit. Edits of one kind close together (a drag, a few taps: under
// kMerge ms) are one step back; a kind marked lasting ("rec": a recording pass) is one step however long it lasts,
// until a new kind or mark() starts the next step.
#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace sloop
{
template <typename State>
class UndoHistory
{
public:
    static constexpr double kMerge = 400.0;              // ms
    static constexpr size_t kDepth = 40;

    // an edit of `kind` is about to change `current` (now: ms)
    void remember (const juce::String& kind, State current, double now, bool lasting = false)
    {
        const bool same = kind == lastKind && (lasting || now - lastEdit < kMerge);
        lastEdit = now;
        lastKind = kind;
        if (same) return;
        back.push_back (std::move (current));
        if (back.size() > kDepth) back.erase (back.begin());
        forward.clear();
    }
    void mark() { lastKind = {}; }                       // the next edit is a step of its own
    void clear() { back.clear(); forward.clear(); lastKind = {}; }
    bool canUndo() const { return ! back.empty(); }
    bool canRedo() const { return ! forward.empty(); }
    // the state to go back to (`current` is kept for redo); nothing: no step back
    std::optional<State> undo (State current) { return travel (back, forward, std::move (current)); }
    std::optional<State> redo (State current) { return travel (forward, back, std::move (current)); }

private:
    std::optional<State> travel (std::vector<State>& from, std::vector<State>& to, State current)
    {
        if (from.empty()) return std::nullopt;
        to.push_back (std::move (current));
        auto s = std::move (from.back());
        from.pop_back();
        lastKind = {};
        return s;
    }
    std::vector<State> back, forward;
    juce::String lastKind;
    double lastEdit = 0;
};
} // namespace sloop
