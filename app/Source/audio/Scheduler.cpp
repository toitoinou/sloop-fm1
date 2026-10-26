// SPDX-License-Identifier: GPL-3.0-only
#include "audio/Scheduler.h"

namespace sloop::scheduler
{
uint32_t hash (uint32_t h)
{
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}

Where locate (const Track& s, int64_t step)
{
    Where w { s.idx, s.start, s.passes, false };
    const Chain* c = &s.chain;
    if (s.pending && step >= s.pendingAt)
    {
        c = &s.next;
        w = { 0, s.pendingAt, 0, true };
    }
    if (c->n == 0) return { -1, step, 0, w.fromNext };
    // the chain goes on clip after clip (a single clip: pass after pass)
    for (int guard = 0; step - w.start >= c->clips[(size_t) w.idx].length && guard < 4096; ++guard)
    {
        w.start += c->clips[(size_t) w.idx].length;
        const int nx = (w.idx + 1) % c->n;
        w.passes = c->rows[(size_t) nx] == c->rows[(size_t) w.idx] ? w.passes + 1 : 0;
        w.idx = nx;
    }
    return w;
}

const ClipData* clipOf (const Track& s, const Where& w)
{
    if (w.idx < 0) return nullptr;
    return &(w.fromNext ? s.next : s.chain).clips[(size_t) w.idx];
}

void commit (Track& s, const Where& w, int64_t step)
{
    if (w.fromNext) s.takeNext();
    s.idx = juce::jmax (0, w.idx);
    s.start = w.start;
    s.passes = w.passes;
    s.cur = step;
}

bool stepPlays (uint32_t seed, int passes, const StepData& x, int lane, int step, bool fill)
{
    if (x.prob < 100 && hash (seed ^ (uint32_t) (lane * 73856093) ^ (uint32_t) (step * 19349663)) % 100u >= x.prob)
        return false;
    switch (x.cond)
    {
        case Cond::oneOfTwo: return passes % 2 == 0;
        case Cond::twoOfTwo: return passes % 2 == 1;
        case Cond::oneOfFour: return passes % 4 == 0;
        case Cond::fill: return fill;
        case Cond::notFill: return ! fill;
        case Cond::always: break;
    }
    return true;
}

StepOut resolve (const Track& s, int64_t step, int track, const Key& key, bool fill)
{
    StepOut out;
    const auto w = locate (s, step);
    const ClipData* c = clipOf (s, w);
    if (c == nullptr) return out;
    const int cs = (int) (step - w.start);
    const uint32_t seed = hash (s.seed ^ (uint32_t) w.start);
    if (isDrum (track))
    {
        for (int lane = 0; lane < kLanes; ++lane)
        {
            const auto& x = c->lanes[(size_t) lane][(size_t) cs];
            if (! x.on || ! stepPlays (seed, w.passes, x, lane, cs, fill)) continue;
            out.lanes = (uint16_t) (out.lanes | 1 << lane);
            out.velocity[(size_t) lane] = x.vel;
            out.ratchet[(size_t) lane] = x.ratchet;
            if (x.nudge != 0) out.nudge = x.nudge;
        }
    }
    else
    {
        // the notes going on from an earlier step (a long note) first, held; then the ones starting here
        for (int r = 0; r < kRows && out.nNotes < 4; ++r)
            if (const int st = c->noteStart (r, cs); st >= 0 && st < cs && stepPlays (seed, w.passes, c->notes[(size_t) st], 0, st, fill))
            {
                out.held |= 1u << out.nNotes;
                out.notes[(size_t) out.nNotes++] = (uint8_t) rowNote (track, r, key);
            }
        const auto& x = c->notes[(size_t) cs];
        if (x.rows != 0 && stepPlays (seed, w.passes, x, 0, cs, fill))
        {
            for (int r = 0; r < kRows && out.nNotes < 4; ++r)
                if (x.rows >> r & 1u) out.notes[(size_t) out.nNotes++] = (uint8_t) rowNote (track, r, key);
            out.vel = x.vel;
            out.ratchetCount = out.held == 0 ? x.ratchet : 1;
            out.nudge = x.nudge;
        }
    }
    for (int i = 0; i < c->nLocks && out.nLocks < (int) out.locks.size(); ++i)
        if (c->locks[(size_t) i].step == cs) out.locks[(size_t) out.nLocks++] = c->locks[(size_t) i];
    return out;
}
} // namespace sloop::scheduler
