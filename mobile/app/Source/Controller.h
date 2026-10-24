// SPDX-License-Identifier: GPL-3.0-only
// What the screens share: the session, the selection, the clips waiting for the bar, the steps held for a
// parameter lock. It turns the screens' gestures into the audio engine's commands.
#pragma once
#include "AudioEngine.h"
#include <functional>

namespace sloop
{
class Controller
{
public:
    explicit Controller (AudioEngine& a) : audio (a), session (demoSession())
    {
        for (int t = 0; t < kTracks; ++t)
        {
            for (int k = 0; k < kKnobs; ++k) base[(size_t) t][(size_t) k] = kKnobDefs[(size_t) k].def;
            if (session.at (t, 0)) { playlist[(size_t) t] = { 0 }; sendPlaylist (t, AudioEngine::When::now); }
        }
    }

    AudioEngine& audio;
    Session session;
    int sel = 0;                                         // the selected track
    std::array<int, kTracks> editSlot {};                // the clip each track edits
    int lane = 0;                                        // the drum sound the knobs and pads point at
    std::array<int, kTracks> queued { { kNone, kNone, kNone, kNone } };   // a row waiting for the bar, kStop, kNone
    std::array<std::vector<int>, kTracks> playlist;      // the rows each track plays in turn (a chain)
    std::array<bool, kTracks> muted {};
    std::array<std::array<int, kKnobs>, kTracks> base;
    std::vector<std::pair<int, int>> held;               // the steps held (row, step): knobs lock them
    bool playing = false;
    int bpm = 118;
    std::function<void()> changed;                       // every screen repaints

    static constexpr int kNone = -2, kStop = -1;

    Clip* editClip (int t)
    {
        auto& c = session.at (t, editSlot[(size_t) t]);
        return c ? &*c : nullptr;
    }
    Clip* editClip() { return editClip (sel); }
    bool editingPlaying() const { return audio.playingSlot[sel].load() == editSlot[(size_t) sel]; }
    void notify() { if (changed) changed(); }

    void select (int t) { sel = t; held.clear(); notify(); }
    void togglePlay()
    {
        playing = ! playing;
        audio.play (playing);
        if (! playing)
            for (int t = 0; t < kTracks; ++t) queued[(size_t) t] = kNone;
        notify();
    }
    void setBpm (int b) { bpm = juce::jlimit (60, 200, b); audio.setBpm (bpm); notify(); }
    void setMute (int t, bool m) { muted[(size_t) t] = m; audio.setParam (t, kParamMute, m ? 1 : 0); notify(); }

    // ---- session
    void launch (int t, int s)
    {
        if (! session.at (t, s)) return;
        editSlot[(size_t) t] = s;
        playlist[(size_t) t] = { s };
        queued[(size_t) t] = playing ? s : kNone;
        sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
        notify();
    }
    void chain (int t, const std::vector<int>& rows)
    {
        playlist[(size_t) t] = rows;
        editSlot[(size_t) t] = rows.front();
        queued[(size_t) t] = playing ? rows.front() : kNone;
        sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
        notify();
    }
    void stopTrack (int t)
    {
        playlist[(size_t) t].clear();
        queued[(size_t) t] = playing ? kStop : kNone;
        sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
    }
    void launchScene (int r)
    {
        for (int t = 0; t < kTracks; ++t)
        {
            if (session.at (t, r)) launch (t, r);
            else stopTrack (t);
        }
        notify();
    }
    void stopAll() { for (int t = 0; t < kTracks; ++t) stopTrack (t); notify(); }
    // the audio thread has taken a queued clip: no more blinking
    void poll()
    {
        bool any = false;
        for (int t = 0; t < kTracks; ++t)
        {
            auto q = queued[(size_t) t];
            const int now = audio.playingSlot[t].load();
            if (q != kNone && (q == now || (q == kStop && now < 0))) { queued[(size_t) t] = kNone; any = true; }
        }
        if (any) notify();
    }
    // an edited clip: the engine gets it at once if it plays (or waits in a chain)
    void clipEdited (int t, int s)
    {
        auto& pl = playlist[(size_t) t];
        if (std::find (pl.begin(), pl.end(), s) != pl.end() && session.at (t, s))
            audio.setPlaylist (t, AudioEngine::When::update, { { s, &session.at (t, s)->data } });
        notify();
    }
    void createClip (int t, int s)
    {
        session.at (t, s) = Session::newClip (t, "Clip " + juce::String (s + 1));
        editSlot[(size_t) t] = s;
        notify();
    }
    void deleteClip (int t, int s)
    {
        session.at (t, s).reset();
        auto& pl = playlist[(size_t) t];
        if (std::find (pl.begin(), pl.end(), s) != pl.end()) stopTrack (t);
        notify();
    }
    void duplicateClip (int t, int s)
    {
        int r = s + 1;
        while (r < session.scenes() && session.at (t, r)) ++r;
        if (r == session.scenes()) session.addScene();
        session.at (t, r) = session.at (t, s);
        session.at (t, r)->name << " bis";
        notify();
    }
    void addScene() { session.addScene(); notify(); }
    void insertSceneCopy (int r, bool capture)
    {
        for (int t = 0; t < kTracks; ++t)
        {
            auto& col = session.cols[(size_t) t];
            const int from = capture ? audio.playingSlot[t].load() : r;
            std::optional<Clip> c = from >= 0 && from < (int) col.size() ? col[(size_t) from] : std::nullopt;
            col.insert (col.begin() + r + 1, c);
        }
        shiftRows (r + 1, 1);
        notify();
    }
    void deleteScene (int r)
    {
        if (session.scenes() <= 1) return;
        for (int t = 0; t < kTracks; ++t)
        {
            auto& pl = playlist[(size_t) t];
            if (std::find (pl.begin(), pl.end(), r) != pl.end()) stopTrack (t);
            session.cols[(size_t) t].erase (session.cols[(size_t) t].begin() + r);
        }
        shiftRows (r + 1, -1);
        notify();
    }

    // ---- steps
    void toggleStep (int row, int step, bool on)
    {
        auto* c = editClip();
        if (c == nullptr) return;
        auto& d = c->data;
        if (d.drum) d.lanes[(size_t) row][(size_t) step].on = on;
        else
        {
            auto& x = d.notes[(size_t) step];
            x.rows = (uint16_t) (on ? x.rows | 1 << row : x.rows & ~(1 << row));
            x.on = x.rows != 0;
        }
        clipEdited (sel, editSlot[(size_t) sel]);
    }
    StepData* step (int row, int s)
    {
        auto* c = editClip();
        if (c == nullptr) return nullptr;
        return c->data.drum ? &c->data.lanes[(size_t) row][(size_t) s] : &c->data.notes[(size_t) s];
    }
    void stepChanged() { clipEdited (sel, editSlot[(size_t) sel]); }

    // ---- knobs: the track's value, or the held steps' locks
    int knob (int k)
    {
        if (! held.empty())
            if (auto* c = editClip())
            {
                auto v = c->data.locks[(size_t) held.front().second][(size_t) k];
                if (v != kNoLock) return v;
            }
        return base[(size_t) sel][(size_t) k];
    }
    bool knobLocked (int k)
    {
        auto* c = editClip();
        return ! held.empty() && c != nullptr && c->data.locks[(size_t) held.front().second][(size_t) k] != kNoLock;
    }
    void setKnob (int k, int v)
    {
        const auto& d = kKnobDefs[(size_t) k];
        v = juce::jlimit (d.min, d.max, v);
        if (! held.empty())
        {
            if (auto* c = editClip())
            {
                for (auto& [row, s] : held) c->data.locks[(size_t) s][(size_t) k] = (int16_t) v;
                lockedSinceHold = true;
                stepChanged();
            }
            return;
        }
        base[(size_t) sel][(size_t) k] = v;
        audio.setParam (sel, d.param, v);
        notify();
    }
    void resetKnob (int k)
    {
        if (! held.empty())
        {
            if (auto* c = editClip())
                for (auto& [row, s] : held) c->data.locks[(size_t) s][(size_t) k] = kNoLock;
            lockedSinceHold = true;
            stepChanged();
            return;
        }
        setKnob (k, kKnobDefs[(size_t) k].def);
    }
    bool lockedSinceHold = false;                        // a knob turned while steps were held

private:
    void sendPlaylist (int t, AudioEngine::When when)
    {
        std::vector<std::pair<int, const ClipData*>> clips;
        for (int s : playlist[(size_t) t])
            if (auto& c = session.at (t, s)) clips.push_back ({ s, &c->data });
        audio.setPlaylist (t, when, clips);
    }
    void shiftRows (int from, int d)
    {
        for (int t = 0; t < kTracks; ++t)
        {
            if (editSlot[(size_t) t] >= from) editSlot[(size_t) t] = juce::jmax (0, editSlot[(size_t) t] + d);
            for (auto& s : playlist[(size_t) t]) if (s >= from) s += d;
            if (queued[(size_t) t] >= from) queued[(size_t) t] += d;
        }
        // the audio thread reports rows: keep it in step with the new numbering
        for (int t = 0; t < kTracks; ++t)
            if (! playlist[(size_t) t].empty()) sendPlaylist (t, AudioEngine::When::now);
    }
};

int runSelfTest();                                       // SelfTest.cpp: the app's --selftest
} // namespace sloop
