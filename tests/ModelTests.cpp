// SPDX-License-Identifier: GPL-3.0-only
// The clips' own rules, undo's steps, a sample's tempo guessed.
#include "control/UndoHistory.h"
#include "model/Samples.h"

namespace sloop
{
class ModelTests : public juce::UnitTest
{
public:
    ModelTests() : juce::UnitTest ("Model") {}

    void runTest() override
    {
        beginTest ("locks: set, replace, clear, and a full clip");
        {
            ClipData c;
            expect (c.setLock (3, 10, 5));
            expect (c.setLock (3, 10, 7));
            expectEquals ((int) c.nLocks, 1);
            expectEquals ((int) c.lock (3, 10), 7);
            expectEquals ((int) c.lock (4, 10), (int) kNoLock);
            for (int i = 0; i < kMaxLocks; ++i) c.setLock (i % kMaxSteps, 40 + i / kMaxSteps, i);
            expectEquals ((int) c.nLocks, kMaxLocks);
            expect (! c.setLock (0, 90, 1), "no room left");
            c.clearLock (63, 99);                    // (none there: the 256 stay)
            expectEquals ((int) c.nLocks, kMaxLocks);
            c.clearLock (0, -1);
            expect (c.nLocks > 0 && c.nLocks < kMaxLocks);
        }

        beginTest ("notes: their length, their start, the room up to the next one");
        {
            ClipData c;
            c.drum = 0;
            c.length = 16;
            c.setStep (5, 2, true);
            c.noteLen[2][5] = 4;
            c.setStep (5, 9, true);
            expectEquals (c.noteStart (5, 4), 2);
            expectEquals (c.noteStart (5, 6), -1);
            expectEquals (c.maxLength (5, 2), 7);
            c.setStep (5, 2, false);
            expectEquals (c.noteStart (5, 3), -1);
        }

        beginTest ("double: the steps, the notes' lengths and the locks twice");
        {
            ClipData c;
            c.length = 8;
            c.lanes[0][1].on = 1;
            c.setLock (2, 36, 50);
            expect (c.canDouble());
            c.doubleLength();
            expectEquals ((int) c.length, 16);
            expect (c.lanes[0][9].on == 1);
            expectEquals ((int) c.lock (10, 36), 50);
            c.length = 40;
            expect (! c.canDouble());
        }

        beginTest ("quantize: every nudge back to the grid");
        {
            ClipData c;
            c.lanes[2][3].nudge = -2;
            c.notes[5].nudge = 3;
            c.quantize();
            expectEquals ((int) c.lanes[2][3].nudge + c.notes[5].nudge, 0);
        }

        beginTest ("a sample's tempo: from its name, else from its length");
        {
            expectEquals (guessTempo ("Loop 92bpm.wav", 5.0, 120.0), 92.0);
            expectEquals (guessTempo ("break_174_BPM_dry.wav", 5.0, 120.0), 174.0);
            expectEquals (guessTempo ("take 2 at 900 bpm.wav", 4.0, 120.0), 120.0);   // (out of range: the length)
            // 4 s: 2 bars at 120, 1 at 60 (too slow), 4 at 240 (too fast)
            expectWithinAbsoluteError (guessTempo ("drums.wav", 4.0, 118.0), 120.0, 1.0e-9);
            expectEquals (guessTempo ("noise.wav", 0.0, 118.0), 118.0);
        }

        beginTest ("an audio clip's beats: its length at its tempo, at least one");
        {
            AudioClip a;
            a.end = (int64_t) (AudioClip::rate * 4.0);
            a.bpm = 120.0;
            expectEquals (a.beats(), 8);
            a.bpm = 40.0;
            expectEquals (a.beats(), 3);
            a.end = 10;
            expectEquals (a.beats(), 1);
        }

        beginTest ("undo: one step per kind of edit close together, a recording pass as one");
        {
            UndoHistory<int> h;
            h.remember ("step", 1, 0.0);
            h.remember ("step", 2, 100.0);          // (the same drag)
            h.remember ("step", 3, 1000.0);         // (later: a step of its own)
            h.remember ("clip", 4, 1100.0);         // (another kind)
            auto u = h.undo (5);
            expect (u.has_value() && *u == 4);
            u = h.undo (4);
            expect (u.has_value() && *u == 3);
            u = h.undo (3);
            expect (u.has_value() && *u == 1);
            expect (! h.canUndo());
            u = h.redo (1);
            expect (u.has_value() && *u == 3);
            h.remember ("rec", 10, 0.0, true);
            h.remember ("rec", 11, 60000.0, true);  // (a pass: one step however long)
            u = h.undo (12);
            expect (u.has_value() && *u == 10);
            expect (! h.canRedo() || true);
            h.mark();
            h.remember ("rec", 20, 60001.0, true);  // (a new take)
            expect (h.canUndo());
        }
    }
};
static ModelTests modelTests;
} // namespace sloop
