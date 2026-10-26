// SPDX-License-Identifier: GPL-3.0-only
// The scheduler's rules, apart from the audio thread and the engine: plain functions on plain data (tests/).
//
// The engine loops each track over its 64 steps; the audio thread writes the few steps just ahead of the playhead
// (AudioEngine), each from the clip due there: the track's chain of clips (one clip: pass after pass) or the chain
// waiting for the bar. A step's dice (its probability) are rolled once a pass, its condition (1:2, 2:2, 1:4, fill)
// read from the pass and the fill.
#pragma once
#include "model/Model.h"

namespace sloop::scheduler
{
uint32_t hash (uint32_t);

// what a track plays in turn: up to kChainMax clips (a row each), copied in
struct Chain
{
    int n = 0;
    std::array<int, kChainMax> rows {};
    std::array<ClipData, kChainMax> clips {};
    bool add (int row, const ClipData& clip)
    {
        if (n == kChainMax) return false;
        rows[(size_t) n] = row;
        clips[(size_t) n++] = clip;
        return true;
    }
};

struct Track
{
    Chain chain;                                         // what plays
    int idx = 0;                                         // the clip playing in it
    int64_t start = 0;                                   // the step (since PLAY) where that clip's pass began
    int passes = 0;                                      // the passes of that clip in a row so far (1:2, 1:4)
    bool pending = false;                                // `next` waits for the step pendingAt (a bar)
    int64_t pendingAt = 0;
    Chain next;
    int64_t cur = -1;                                    // the step playing, since PLAY (-1: none yet)
    int64_t written = -1;                                // the engine has the steps up to there
    int lastPos = -1;                                    // the engine's step (0..63) last seen
    uint32_t seed = 1;                                   // the dice of the passes: one roll a pass

    void takeNext() { chain = next; pending = false; }
    void rewind() { idx = 0; passes = 0; start = 0; cur = -1; lastPos = -1; }
    int playingRow() const { return chain.n > 0 ? chain.rows[(size_t) idx] : -1; }
};

// the clip due at a step and its pass (idx -1: nothing plays there; fromNext: from the chain waiting)
struct Where { int idx = -1; int64_t start = 0; int passes = 0; bool fromNext = false; };
Where locate (const Track&, int64_t step);
const ClipData* clipOf (const Track&, const Where&);
void commit (Track&, const Where&, int64_t step);        // the playhead is at `step`, where `w` says

bool stepPlays (uint32_t seed, int passes, const StepData&, int lane, int step, bool fill);

// what the engine gets for one step of a track: the drum hits or the synth notes, and the step's locks
struct StepOut
{
    // drums
    uint16_t lanes = 0;
    std::array<uint8_t, kLanes> velocity {}, ratchet {};
    // synth: up to 4 notes, bit k of `held` = note k goes on from the step before (a long note)
    int nNotes = 0;
    std::array<uint8_t, 4> notes {};
    unsigned held = 0;
    int vel = 100, ratchetCount = 1;
    int nudge = 0;                                       // sixths of a step
    int nLocks = 0;
    std::array<LockData, 24> locks {};                   // (sloop_engine.h SE_MAX_LOCKS)
};
StepOut resolve (const Track&, int64_t step, int track, const Key&, bool fill);
} // namespace sloop::scheduler
