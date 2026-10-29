// SPDX-License-Identifier: GPL-3.0-only
// The controller's core: the transport, projects, what the audio thread reports (poll), undo.
#include "control/Controller.h"

namespace sloop
{
Controller::Controller (AudioEngine& a, bool restore) : audio (a)
{
    readParams();
    for (int id : kMixGlobals) { int def = 0; se_global_describe (id, nullptr, nullptr, &def, nullptr); globals[(size_t) id] = def; }
    if (restore) prefs = store::readPrefs();
    audio.setMetronome (prefs.metronome);
    if (prefs.link) audio.setLink (true);
    if (auto saved = restore ? store::read (store::autosaveFile()) : std::nullopt) load (*saved);
    else
    {
        Project demo;
        demo.name = juce::String::fromUTF8 ("Démo");
        demo.session = demoSession();
        load (demo);
    }
}

void Controller::togglePlay()
{
    playing = ! playing;
    audio.play (playing, playing && recording && prefs.countIn);   // (recording: a bar of clicks first)
    if (! playing)
    {
        for (int t = 0; t < kTracks; ++t) queued[(size_t) t] = kNone;
        audioQueued = kNone;
    }
    notify();
}

void Controller::setPrefs (const Prefs& p)
{
    if (p.link != prefs.link) audio.setLink (p.link);
    prefs = p;
    audio.setMetronome (prefs.metronome);
    store::writePrefs (prefs);
    notify();
}

void Controller::setSound (int t, int engine, int preset)
{
    sounds[(size_t) t] = { engine, preset };
    if (t == sel)                                    // (a page the new engine does not have: its main one)
    {
        const auto ps = pages();
        if (std::find (ps.begin(), ps.end(), page[(size_t) t]) == ps.end()) page[(size_t) t] = Page::macro;
    }
    if (isDrum (t)) audio.setKit (preset);
    else audio.setSound (t, engine, preset);
    notify();
}

Project Controller::snapshot() const
{
    Project p;
    p.name = projectName;
    p.bpm = bpm;
    p.swing = swing;
    p.key = key;
    p.masterVolume = masterVolume;
    for (int id : kMixGlobals) p.globals[id] = globals[(size_t) id];
    p.session = session;
    p.sounds = sounds;
    p.params = params;
    p.hasParams.fill (true);
    p.audioLevel = audioLevel;
    p.audioPan = audioPan;
    p.audioMuted = audioMuted;
    p.tracks = trackCount;
    p.audioTrack = audioShown;
    return p;
}

void Controller::load (const Project& p)
{
    if (playing) togglePlay();
    session = p.session;
    if (session.scenes() == 0) session.addScene();
    projectName = p.name;
    trackCount = juce::jlimit (kMinTracks, kTracks, p.tracks);
    audioShown = p.audioTrack;
    sel = 0;
    lane = 0;
    held.clear();
    pendingRecord.clear();
    recordedHeld.clear();
    recording = false;
    editSlot.fill (0);
    queued.fill (kNone);
    page = firstPages();
    history.clear();
    setBpm (p.bpm);
    setSwing (p.swing);
    setKey (p.key);
    setMasterVolume (p.masterVolume);
    soloMask = 0;
    audio.setSolo (0);
    for (int id : kMixGlobals)
    {
        int def = 0;
        se_global_describe (id, nullptr, nullptr, &def, nullptr);
        setGlobal (id, p.globals.count (id) ? p.globals.at (id) : def);
    }
    for (int t = 0; t < kTracks; ++t)
    {
        const auto snd = p.sounds[(size_t) t];
        sounds[(size_t) t] = snd;
        if (isDrum (t)) audio.setKit (snd.preset);
        else audio.setSound (t, snd.engine, snd.preset);
        if (p.hasParams[(size_t) t])                 // (after the sound: the saved values over the preset's)
        {
            params[(size_t) t] = p.params[(size_t) t];
            audio.setParams (t, p.params[(size_t) t]);
        }
        muted[(size_t) t] = p.hasParams[(size_t) t] && p.params[(size_t) t][kParamMute] != 0;
        playlist[(size_t) t].clear();
        if (session.at (t, 0)) playlist[(size_t) t] = { 0 };
        sendPlaylist (t, AudioEngine::When::now);
    }
    session.audio.resize ((size_t) session.scenes());
    std::set<juce::String> files;
    for (auto& c : session.audio) if (c) files.insert (c->file);
    samples.keepOnly (files);
    audioQueued = kNone;
    audioRow = -1;
    audio.setAudioClip (AudioEngine::When::now, -1, {});
    if (session.audio[0]) launchAudio (0);
    setAudioMix (p.audioLevel, p.audioPan, p.audioMuted);
    notify();
}

void Controller::poll()
{
    bool any = false;
    recordRepeats();
    if (audio.paramSerial.load() != seenParamSerial) { readParams(); any = true; }   // a sound brought its own values
    if (const int n = prefs.link ? audio.link.peers() : -1; n != linkPeers) { linkPeers = n; any = true; }
    if (const int ls = audio.linkSerial.load(); ls != seenLinkSerial)
    {
        // Link: what the session changed (another app's tempo, PLAY or STOP; this app's own come back the same)
        seenLinkSerial = ls;
        if (const double lt = audio.linkTempo.load(); std::abs (lt - seenLinkTempo) > 1.0e-6)
        {
            seenLinkTempo = lt;
            bpm = juce::jlimit (kMinBpm, kMaxBpm, juce::roundToInt (lt));
            any = true;
        }
        if (const bool lp = audio.linkPlaying.load(); lp != seenLinkPlaying)
        {
            seenLinkPlaying = lp;
            if (lp != playing)
            {
                playing = lp;
                if (! playing) { queued.fill (kNone); audioQueued = kNone; }
                any = true;
            }
        }
    }
    for (auto it = pendingRecord.begin(); it != pendingRecord.end();)
        if (audio.position[it->track].load() != it->step || ! playing)
        {
            clipEdited (it->track, it->slot);
            it = pendingRecord.erase (it);
        }
        else ++it;
    for (int t = 0; t < kTracks; ++t)
    {
        auto q = queued[(size_t) t];
        const int now = audio.playingSlot[t].load();
        if (q != kNone && (q == now || (q == kStop && now < 0))) { queued[(size_t) t] = kNone; any = true; }
    }
    if (const int now = audio.audioSlot.load(); audioQueued != kNone && (audioQueued == now || (audioQueued == kStop && now < 0)))
    {
        audioQueued = kNone;
        any = true;
    }
    if (any) notify();
}

// ---- undo / redo
void Controller::remember (const char* kind)
{
    const juce::String k (kind);
    history.remember (k, state(), juce::Time::getMillisecondCounterHiRes(), k == "rec");
}
void Controller::undo() { if (auto u = history.undo (state())) restore (std::move (*u)); }
void Controller::redo() { if (auto u = history.redo (state())) restore (std::move (*u)); }

void Controller::restore (UndoState u)
{
    trackCount = u.trackCount;
    audioShown = u.audioShown;
    if (sel >= trackCount) sel = 0;
    held.clear();
    pendingRecord.clear();
    recordedHeld.clear();
    session = std::move (u.session);
    for (int t = 0; t < kTracks; ++t)
    {
        editSlot[(size_t) t] = juce::jlimit (0, session.scenes() - 1, u.editSlot[(size_t) t]);
        // what plays goes on, with the clips as they were (a clip that is gone stops)
        std::vector<int> pl;
        for (int s : u.playlist[(size_t) t])
            if (s < session.scenes() && session.at (t, s)) pl.push_back (s);
        if (pl != playlist[(size_t) t])
        {
            playlist[(size_t) t] = pl;
            queued[(size_t) t] = kNone;
            sendPlaylist (t, AudioEngine::When::now);
        }
        else sendPlaylist (t, AudioEngine::When::update);
    }
    const int row = u.audioRow >= 0 && u.audioRow < session.scenes() && session.audio[(size_t) u.audioRow] ? u.audioRow : -1;
    if (row != audioRow)
    {
        audioQueued = kNone;
        if (row < 0) { audioRow = -1; audio.setAudioClip (AudioEngine::When::now, -1, {}); }
        else { audioRow = row; audio.setAudioClip (AudioEngine::When::now, row, playerClip (*session.audio[(size_t) row])); }
    }
    else if (row >= 0) audioEdited (row);
    notify();
}

void Controller::readParams()
{
    seenParamSerial = audio.paramSerial.load();
    for (int t = 0; t < kTracks; ++t)
        for (int i = 0; i < kParamSlots; ++i) params[(size_t) t][(size_t) i] = audio.paramValue[t][i].load();
}

void Controller::sendPlaylist (int t, AudioEngine::When when)
{
    std::vector<std::pair<int, const ClipData*>> clips;
    for (int s : playlist[(size_t) t])
        if (auto& c = session.at (t, s)) clips.push_back ({ s, &c->data });
    audio.setPlaylist (t, when, clips);
}

void Controller::shiftRows (int from, int d)
{
    for (int t = 0; t < kTracks; ++t)
    {
        if (editSlot[(size_t) t] >= from) editSlot[(size_t) t] = juce::jmax (0, editSlot[(size_t) t] + d);
        for (auto& s : playlist[(size_t) t]) if (s >= from) s += d;
        if (queued[(size_t) t] >= from) queued[(size_t) t] += d;
    }
    if (audioRow >= from) audioRow += d;
    if (audioQueued >= from) audioQueued += d;
    // the audio thread reports rows: keep it in step with the new numbering
    audio.renumber (from, d);
}

// ---- the tracks shown
std::vector<int> Controller::columns() const
{
    std::vector<int> c;
    for (int t = 0; t < trackCount; ++t) c.push_back (t);
    if (audioShown) c.push_back (kAudio);
    return c;
}

void Controller::addTrack()
{
    if (! canAddTrack()) return;
    remember ("track");
    select (trackCount++);
}

void Controller::removeTrack()
{
    if (trackCount <= kMinTracks) return;
    remember ("track");
    const int t = --trackCount;
    stopTrack (t);
    for (auto& c : session.cols[(size_t) t]) c.reset();
    if (soloMask >> t & 1u) toggleSolo (t);
    if (sel >= trackCount) select (0);
    notify();
}

void Controller::showAudioTrack (bool on)
{
    if (on == audioShown) return;
    remember ("track");
    audioShown = on;
    if (! on)
    {
        stopAudio();
        for (auto& c : session.audio) c.reset();
        if (soloMask >> kAudio & 1u) toggleSolo (kAudio);
    }
    notify();
}
} // namespace sloop
