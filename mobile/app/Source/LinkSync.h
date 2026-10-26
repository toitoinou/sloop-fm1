// SPDX-License-Identifier: GPL-3.0-only
// Ableton Link (third_party/link, GPL-2.0-or-later): the tempo, the bar's phase and PLAY / STOP shared with the
// other Link apps on the same network (Live, Bitwig, Koala, G-Stomper...). Link's headers (and Asio's) are only
// seen by LinkSync.cpp.
//
// Times are Link's clock, in microseconds. The beats are counted with a quantum of a bar (4 beats): every app on the
// network has the bar's first beat at the same moment.
#pragma once
#include <cstdint>
#include <memory>

namespace sloop
{
class LinkSync
{
public:
    explicit LinkSync (double bpm);
    ~LinkSync();

    // ---- any thread
    void enable (bool on);
    bool isEnabled() const;
    int peers() const;                                   // the other apps in the session

    // ---- the audio thread (no lock, no allocation)
    static constexpr double kQuantum = 4.0;
    int64_t now() const;
    // the time the device's sample `frame` (counted since the device started) leaves the device, filtered (the
    // callbacks do not come at regular times)
    int64_t frameTime (double frame);
    void resetFilter();
    struct State
    {
        bool playing = false;
        double tempo = 120.0;
        double beatStart = 0, beatEnd = 0;               // the beats at the two times asked
    };
    State capture (int64_t start, int64_t end) const;
    void setTempo (double bpm, int64_t at);
    void setPlaying (bool on, int64_t at);               // PLAY: the beat 0 there (alone), else the next bar's start

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sloop
