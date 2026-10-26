// SPDX-License-Identifier: GPL-3.0-only
// The session: launching clips and scenes, chains, clips and scenes edited, the steps of the clip edited.
#include "control/Controller.h"

namespace sloop
{
void Controller::launch (int t, int s)
{
    if (! session.at (t, s)) return;
    editSlot[(size_t) t] = s;
    playlist[(size_t) t] = { s };
    queued[(size_t) t] = playing ? s : kNone;
    sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
    notify();
}

void Controller::chain (int t, const std::vector<int>& rows)
{
    playlist[(size_t) t] = rows;
    editSlot[(size_t) t] = rows.front();
    queued[(size_t) t] = playing ? rows.front() : kNone;
    sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
    notify();
}

void Controller::stopTrack (int t)
{
    playlist[(size_t) t].clear();
    queued[(size_t) t] = playing ? kStop : kNone;
    sendPlaylist (t, playing ? AudioEngine::When::nextBar : AudioEngine::When::now);
}

void Controller::launchScene (int r)
{
    for (int t = 0; t < kTracks; ++t)
    {
        if (session.at (t, r)) launch (t, r);
        else stopTrack (t);
    }
    if (session.audio[(size_t) r]) launchAudio (r);
    else stopAudio();
    notify();
}

void Controller::stopAll() { for (int t = 0; t < kTracks; ++t) stopTrack (t); stopAudio(); notify(); }

void Controller::clipEdited (int t, int s)
{
    auto& pl = playlist[(size_t) t];
    if (std::find (pl.begin(), pl.end(), s) != pl.end() && session.at (t, s))
        audio.setPlaylist (t, AudioEngine::When::update, { { s, &session.at (t, s)->data } });
    notify();
}

void Controller::createClip (int t, int s)
{
    remember ("clip");
    session.at (t, s) = Session::newClip (t, "Clip " + juce::String (s + 1));
    editSlot[(size_t) t] = s;
    notify();
}

void Controller::deleteClip (int t, int s)
{
    remember ("clip");
    session.at (t, s).reset();
    auto& pl = playlist[(size_t) t];
    if (std::find (pl.begin(), pl.end(), s) != pl.end()) stopTrack (t);
    notify();
}

void Controller::duplicateClip (int t, int s)
{
    remember ("clip");
    int r = s + 1;
    while (r < session.scenes() && session.at (t, r)) ++r;
    if (r == session.scenes()) session.addScene();
    session.at (t, r) = session.at (t, s);
    session.at (t, r)->name << " bis";
    notify();
}

void Controller::insertSceneCopy (int r, bool capture)
{
    remember ("scene");
    for (int t = 0; t < kTracks; ++t)
    {
        auto& col = session.cols[(size_t) t];
        const int from = capture ? audio.playingSlot[t].load() : r;
        std::optional<Clip> c = from >= 0 && from < (int) col.size() ? col[(size_t) from] : std::nullopt;
        col.insert (col.begin() + r + 1, c);
    }
    {
        const int from = capture ? audio.audioSlot.load() : r;
        auto& col = session.audio;
        std::optional<AudioClip> c = from >= 0 && from < (int) col.size() ? col[(size_t) from] : std::nullopt;
        col.insert (col.begin() + r + 1, c);
    }
    shiftRows (r + 1, 1);
    notify();
}

void Controller::deleteScene (int r)
{
    if (session.scenes() <= 1) return;
    remember ("scene");
    for (int t = 0; t < kTracks; ++t)
    {
        auto& pl = playlist[(size_t) t];
        if (std::find (pl.begin(), pl.end(), r) != pl.end()) stopTrack (t);
        session.cols[(size_t) t].erase (session.cols[(size_t) t].begin() + r);
    }
    if (audioRow == r) stopAudio();
    session.audio.erase (session.audio.begin() + r);
    shiftRows (r + 1, -1);
    notify();
}

void Controller::pasteClip (int t, int s)
{
    if (! canPaste (t)) return;
    remember ("clip");
    session.at (t, s) = clipboard;
    editSlot[(size_t) t] = s;
    clipEdited (t, s);
}

void Controller::setLength (int t, int s, int len)
{
    auto& c = session.at (t, s);
    if (! c) return;
    remember ("length");
    c->data.length = (uint8_t) juce::jlimit (1, kMaxSteps, len);
    clipEdited (t, s);
}

void Controller::doubleClip (int t, int s)
{
    auto& c = session.at (t, s);
    if (! c || ! c->data.canDouble()) return;
    remember ("length");
    c->data.doubleLength();
    clipEdited (t, s);
}

void Controller::quantizeClip (int t, int s)
{
    auto& c = session.at (t, s);
    if (! c) return;
    remember ("quantize");
    c->data.quantize();
    clipEdited (t, s);
}

void Controller::toggleStep (int row, int step, bool on)
{
    auto* c = editClip();
    if (c == nullptr) return;
    remember ("step");
    c->data.setStep (row, step, on);
    clipEdited (sel, editSlot[(size_t) sel]);
}

void Controller::setNoteLength (int row, int start, int len)
{
    auto* c = editClip();
    if (c == nullptr || c->data.drum) return;
    len = juce::jlimit (1, c->data.maxLength (row, start), len);
    if (c->data.noteLength (start, row) == len) return;
    remember ("length");
    c->data.noteLen[(size_t) start][(size_t) row] = (uint8_t) len;
    stepChanged();
}

int Controller::noteAt (int row, int step)
{
    auto* c = editClip();
    return c != nullptr && ! c->data.drum ? c->data.noteStart (row, step) : -1;
}

StepData* Controller::step (int row, int s)
{
    auto* c = editClip();
    if (c == nullptr) return nullptr;
    return c->data.drum ? &c->data.lanes[(size_t) row][(size_t) s] : &c->data.notes[(size_t) s];
}
} // namespace sloop
