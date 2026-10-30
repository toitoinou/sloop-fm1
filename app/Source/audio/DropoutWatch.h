// SPDX-License-Identifier: GPL-3.0-only
// The audio's drop-outs (xruns: the device wanted audio and none was ready, a crackle): the device's running count,
// read now and then. A few within a short while, and the next buffer size up is worth offering. A device that has
// just started or changed its buffer is let settle first (its first blocks can drop out once).
#pragma once
#include <deque>

namespace sloop
{
class DropoutWatch
{
public:
    static constexpr double kWindow = 10000.0;           // ms: the drop-outs counted over this
    static constexpr int kEnough = 3;                    // as many as this in it: offer a larger buffer
    static constexpr double kSettle = 3000.0;            // ms after a (re)start: not counted

    void restart (double now)                            // the device (re)started: a new count, settling
    {
        events.clear();
        last = -1;
        quietUntil = now + kSettle;
    }
    // the device's count now (-1: it does not count); true: enough drop-outs lately (the window starts again)
    bool sample (double now, int count)
    {
        if (count < 0) return false;
        if (count < last) restart (now);                 // (the stream started over by itself: settling again)
        if (last < 0) { last = count; return false; }   // (the first look: the count so far)
        const int fresh = count - last;
        last = count;
        if (now < quietUntil || fresh <= 0) return false;
        total += fresh;
        events.push_back ({ now, fresh });
        while (! events.empty() && now - events.front().time > kWindow) events.pop_front();
        int recent = 0;
        for (auto& e : events) recent += e.count;
        if (recent < kEnough) return false;
        lastRecent = recent;
        events.clear();
        return true;
    }
    int totalSinceStart() const { return total; }        // (shown in the settings)
    int recentCount() const { return lastRecent; }       // (the ones that made sample() say yes)

private:
    struct Event { double time; int count; };
    std::deque<Event> events;
    int last = -1, total = 0, lastRecent = 0;
    double quietUntil = 0;
};
} // namespace sloop
