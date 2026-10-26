// SPDX-License-Identifier: GPL-3.0-only
// The scheduler's rules: which clip plays at a step (chains, passes, the chain waiting for the bar), a step's dice
// and conditions, what the engine gets for a step.
#include "audio/Scheduler.h"

namespace sloop
{
namespace
{
using namespace scheduler;

ClipData drumClip (int length, std::initializer_list<int> kicks)
{
    ClipData c;
    c.drum = 1;
    c.length = (uint8_t) length;
    for (int s : kicks) c.lanes[0][(size_t) s].on = 1;
    return c;
}
ClipData synthClip (int length)
{
    ClipData c;
    c.drum = 0;
    c.length = (uint8_t) length;
    return c;
}
} // namespace

class SchedulerTests : public juce::UnitTest
{
public:
    SchedulerTests() : juce::UnitTest ("Scheduler") {}

    void runTest() override
    {
        beginTest ("a single clip loops pass after pass");
        {
            Track t;
            t.chain.add (0, drumClip (16, { 0 }));
            auto w = locate (t, 0);
            expectEquals (w.idx, 0);
            expectEquals ((int) w.start, 0);
            w = locate (t, 37);
            expectEquals ((int) w.start, 32);
            expectEquals (w.passes, 2);
        }

        beginTest ("a chain mixes lengths, clip after clip");
        {
            Track t;
            t.chain.add (3, drumClip (8, {}));
            t.chain.add (5, drumClip (12, {}));
            expectEquals (locate (t, 7).idx, 0);
            const auto w = locate (t, 8);
            expectEquals (w.idx, 1);
            expectEquals ((int) w.start, 8);
            expectEquals (locate (t, 19).idx, 1);
            const auto again = locate (t, 20);       // back to the first: 8 + 12
            expectEquals (again.idx, 0);
            expectEquals (again.passes, 0);          // (another row came between: a new pass count)
        }

        beginTest ("a chain waiting for the bar starts there");
        {
            Track t;
            t.chain.add (0, drumClip (16, {}));
            t.next.add (1, drumClip (4, {}));
            t.pending = true;
            t.pendingAt = 16;
            expect (! locate (t, 15).fromNext);
            const auto w = locate (t, 18);
            expect (w.fromNext);
            expectEquals ((int) w.start, 16);
            commit (t, w, 18);
            expect (! t.pending);
            expectEquals (t.playingRow(), 1);
        }

        beginTest ("nothing plays on an empty chain");
        {
            Track t;
            expectEquals (locate (t, 5).idx, -1);
            expect (clipOf (t, locate (t, 5)) == nullptr);
            expectEquals ((int) resolve (t, 5, 1, Key {}, false).nNotes, 0);
        }

        beginTest ("conditions follow the passes and the fill");
        {
            StepData x;
            x.cond = Cond::oneOfTwo;
            expect (stepPlays (1, 0, x, 0, 0, false));
            expect (! stepPlays (1, 1, x, 0, 0, false));
            x.cond = Cond::twoOfTwo;
            expect (stepPlays (1, 1, x, 0, 0, false));
            x.cond = Cond::oneOfFour;
            expect (stepPlays (1, 4, x, 0, 0, false) && ! stepPlays (1, 2, x, 0, 0, false));
            x.cond = Cond::fill;
            expect (stepPlays (1, 0, x, 0, 0, true) && ! stepPlays (1, 0, x, 0, 0, false));
            x.cond = Cond::notFill;
            expect (! stepPlays (1, 0, x, 0, 0, true));
        }

        beginTest ("a probability plays about that often, the same way for the same pass");
        {
            StepData x;
            x.prob = 30;
            int played = 0;
            for (uint32_t seed = 0; seed < 2000; ++seed) played += stepPlays (hash (seed), 0, x, 2, 5, false) ? 1 : 0;
            expect (played > 500 && played < 700, "about 30 % of 2000: " + juce::String (played));
            expect (stepPlays (77, 0, x, 2, 5, false) == stepPlays (77, 0, x, 2, 5, false));
        }

        beginTest ("a drum step: its hits, velocity, ratchet and nudge");
        {
            Track t;
            auto c = drumClip (16, { 4 });
            c.lanes[3][4] = { 1, 90, 3, 100, 2, Cond::always, 0 };
            t.chain.add (0, c);
            const auto out = resolve (t, 4, 0, Key {}, false);
            expectEquals ((int) out.lanes, (1 << 0) | (1 << 3));
            expectEquals ((int) out.velocity[3], 90);
            expectEquals ((int) out.ratchet[3], 3);
            expectEquals (out.nudge, 2);
            expectEquals ((int) resolve (t, 5, 0, Key {}, false).lanes, 0);
        }

        beginTest ("a long synth note is held over the next steps, not played again");
        {
            Track t;
            auto c = synthClip (16);
            c.setStep (2, 0, true);
            c.noteLen[0][2] = 3;
            t.chain.add (0, c);
            const Key key;
            const auto first = resolve (t, 0, 1, key, false);
            expectEquals (first.nNotes, 1);
            expectEquals ((int) first.held, 0);
            expectEquals ((int) first.notes[0], rowNote (1, 2, key));
            const auto next = resolve (t, 1, 1, key, false);
            expectEquals (next.nNotes, 1);
            expectEquals ((int) next.held, 1);
            expectEquals (resolve (t, 3, 1, key, false).nNotes, 0);
        }

        beginTest ("a step's locks go with it");
        {
            Track t;
            auto c = drumClip (16, {});
            c.setLock (6, 36, 90);
            c.setLock (7, 36, 10);
            t.chain.add (0, c);
            const auto out = resolve (t, 6, 0, Key {}, false);
            expectEquals (out.nLocks, 1);
            expectEquals ((int) out.locks[0].value, 90);
        }
    }
};
static SchedulerTests schedulerTests;
} // namespace sloop
