// SPDX-License-Identifier: GPL-3.0-only
// Recording: pads, keys and MIDI into the clip that plays, knob moves as locks, note repeat.
#include "control/Controller.h"

namespace sloop
{
void Controller::recordMotion (int param, int value)
{
    if (! recording || ! playing || audio.countIn.load() > 0) return;
    const int slot = audio.playingSlot[sel].load(), pos = audio.position[sel].load();
    if (slot < 0 || pos < 0 || ! session.at (sel, slot)) return;
    auto& d = session.at (sel, slot)->data;
    if (d.lock (pos, param) == value) return;
    remember ("rec");
    if (d.setLock (pos, param, value)) clipEdited (sel, slot);
}

// every lock of a clip (its recorded moves, its held steps')
void Controller::clearMotion (int t, int s)
{
    if (! session.at (t, s)) return;
    remember ("clear");
    session.at (t, s)->data.nLocks = 0;
    clipEdited (t, s);
}

void Controller::toggleRecord()
{
    recording = ! recording;
    history.mark();                                  // (a new take: its own step back)
    if (recording && ! playing) togglePlay();
    else notify();
}

void Controller::recordHit (int row, int velocity)
{
    if (! recording || ! playing || audio.countIn.load() > 0) return;
    const int t = sel;
    int slot = audio.playingSlot[t].load();
    remember ("rec");
    int pos = audio.position[t].load();
    if (slot < 0)                                    // nothing plays on the track: a new clip from this bar, this hit in it
    {
        pos = audio.barStep[t].load();
        if (pos < 0) return;
        slot = editSlot[(size_t) t];
        if (session.at (t, slot)) { slot = 0; while (slot < session.scenes() && session.at (t, slot)) ++slot; }
        if (slot == session.scenes()) session.addScene();
        session.at (t, slot) = Session::newClip (t, "Rec " + juce::String (slot + 1));
        editSlot[(size_t) t] = slot;
        playlist[(size_t) t] = { slot };
        sendPlaylist (t, AudioEngine::When::now);    // (from the start of the bar playing: its steps line up)
    }
    auto& clip = session.at (t, slot);
    if (! clip) return;
    if (pos < 0) return;
    const float phase = audio.stepPhase[t].load();
    const bool late = phase >= 0.5f;                 // nearer the next step
    const int step = late ? (pos + 1) % clip->data.length : pos;
    // not quantized: the hit keeps its place between the steps (a nudge, in sixths of a step)
    const int nudge = prefs.quantizeRec ? 0 : juce::roundToInt ((late ? phase - 1.0f : phase) * 6.0f);
    auto& d = clip->data;
    if (d.drum) { auto& x = d.lanes[(size_t) row][(size_t) step]; x.on = 1; x.vel = (uint8_t) velocity; x.nudge = (int8_t) juce::jlimit (-3, 3, nudge); }
    else
    {
        if (const int st = d.noteStart (row, step); st >= 0 && st < step)   // a held note there ends before it
            d.noteLen[(size_t) st][(size_t) row] = (uint8_t) (step - st);
        auto& x = d.notes[(size_t) step];
        x.rows |= 1u << row;
        x.on = 1;
        x.vel = (uint8_t) velocity;
        x.nudge = (int8_t) juce::jlimit (-3, 3, nudge);
        d.noteLen[(size_t) step][(size_t) row] = 1;
        recordedHeld.push_back ({ t, slot, step, row });   // (its length when the key is let go: recordRelease)
    }
    if (late) pendingRecord.push_back ({ t, slot, step });   // (it already sounded: not twice)
    else clipEdited (t, slot);
    notify();
}

void Controller::midiNote (int note, int velocity)
{
    if (isDrum (sel))
    {
        if (velocity > 0)
            for (int l = 0; l < kLanes; ++l)
                if (se_lane_note (l) == note) { lane = l; recordHit (l, velocity); notify(); break; }
        return;
    }
    int row = 0, best = 1000;                        // the piano roll's row nearest to the note (the key's)
    for (int r = 0; r < kRows; ++r)
        if (const int d = std::abs (rowNote (sel, r, key) - note); d < best) { best = d; row = r; }
    if (velocity > 0) recordHit (row, velocity);
    else recordRelease (sel, row);
}

void Controller::midiCC (int cc, int value)
{
    const int k = cc >= 21 && cc <= 24 ? cc - 21 : cc >= 70 && cc <= 73 ? cc - 70 : -1;
    if (k < 0) return;
    const auto d = knobDefs()[(size_t) k];
    if (d.valid) setKnob (k, d.min + juce::roundToInt ((float) value * (float) (d.max - d.min) / 127.0f));
}

void Controller::repeatPress (int t, int row, int id, int velocity)
{
    audio.repeat (t, id, velocity, repeatRate, true);
    heldRepeats.push_back ({ t, row, id, velocity, audio.barStep[t].load() });
}

void Controller::repeatRelease (int t, int id)
{
    audio.repeat (t, id, 0, repeatRate, false);
    heldRepeats.erase (std::remove_if (heldRepeats.begin(), heldRepeats.end(),
                                       [t, id] (const HeldRepeat& h) { return h.track == t && h.id == id; }),
                       heldRepeats.end());
}

void Controller::recordRepeats()
{
    for (auto& h : heldRepeats)
    {
        const int bar = audio.barStep[h.track].load(), pos = audio.position[h.track].load();
        const int slot = audio.playingSlot[h.track].load();
        if (bar < 0 || bar == h.lastBarStep) continue;
        const int passed = h.lastBarStep < 0 ? 0 : (bar - h.lastBarStep + kBar) % kBar;
        h.lastBarStep = bar;
        if (! recording || ! playing || audio.countIn.load() > 0 || slot < 0 || pos < 0 || ! session.at (h.track, slot)) continue;
        auto& d = session.at (h.track, slot)->data;
        const double rate = repeatSteps (repeatRate);
        for (int k = passed; k >= 1; --k)            // the steps just finished (the last one: one step back)
        {
            const int b = (bar - k + kBar) % kBar, st = (pos - k + 2 * d.length) % d.length;
            int n = 0;
            double first = -1;
            for (double x = std::ceil (b / rate) * rate; x < b + 1.0 - 1e-9; x += rate, ++n)
                if (first < 0) first = x - b;
            if (n == 0) continue;
            remember ("rec");
            const int nudge = n > 1 ? 0 : juce::jlimit (0, 3, juce::roundToInt (first * 6.0));
            StepData* x = nullptr;
            if (d.drum) x = &d.lanes[(size_t) h.row][(size_t) st];
            else
            {
                x = &d.notes[(size_t) st];
                x->rows |= 1u << h.row;
                d.noteLen[(size_t) st][(size_t) h.row] = 1;
            }
            x->on = 1;
            x->vel = (uint8_t) h.vel;
            x->ratchet = (uint8_t) juce::jlimit (1, 4, n);
            x->nudge = (int8_t) nudge;
        }
        clipEdited (h.track, slot);
    }
}

void Controller::recordRelease (int t, int row)
{
    auto it = std::find_if (recordedHeld.begin(), recordedHeld.end(),
                            [t, row] (const RecHeld& h) { return h.track == t && h.row == row; });
    if (it == recordedHeld.end()) return;
    const auto h = *it;
    recordedHeld.erase (it);
    auto& clip = session.at (h.track, h.slot);
    const int pos = audio.position[h.track].load();
    if (! clip || pos < 0 || audio.playingSlot[h.track].load() != h.slot) return;
    auto& d = clip->data;
    if (! (d.notes[(size_t) h.step].rows >> h.row & 1u)) return;
    int len = pos + (audio.stepPhase[h.track].load() >= 0.5f ? 1 : 0) - h.step;
    if (len < 0) len += d.length;                    // (held over the loop's end)
    len = juce::jlimit (1, d.maxLength (h.row, h.step), len);
    if (len <= 1 || d.noteLength (h.step, h.row) == len) return;
    d.noteLen[(size_t) h.step][(size_t) h.row] = (uint8_t) len;
    clipEdited (h.track, h.slot);
}
} // namespace sloop
