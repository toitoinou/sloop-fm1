// SPDX-License-Identifier: GPL-3.0-only
// The mix: levels, pans, sends, mute and solo, the master, the audio track's own mix.
#include "control/Controller.h"

namespace sloop
{
void Controller::setMute (int t, bool m)
{
    muted[(size_t) t] = m;
    params[(size_t) t][kParamMute] = m ? 1 : 0;
    audio.setParam (t, kParamMute, m ? 1 : 0);
    notify();
}

void Controller::setAudioMix (int level, int pan, bool mute)
{
    audioLevel = juce::jlimit (0, 127, level);
    audioPan = juce::jlimit (-64, 63, pan);
    audioMuted = mute;
    const float f = (float) audioLevel / 127.0f;
    audio.audioLevel = f * f;
    audio.audioPan = (float) audioPan / 64.0f;
    audio.audioMute = audioMuted;
    notify();
}

void Controller::setGlobal (int id, int v)
{
    int lo = 0, hi = 127;
    se_global_describe (id, &lo, &hi, nullptr, nullptr);
    globals[(size_t) id] = juce::jlimit (lo, hi, v);
    audio.setGlobal (id, globals[(size_t) id]);
    notify();
}

void Controller::setMasterVolume (int v)
{
    masterVolume = juce::jlimit (0, 127, v);
    const float g = (float) masterVolume / 100.0f;
    audio.masterGain = g * g;
    notify();
}

// (the mixer: no lock, no move recorded)
void Controller::setTrackParam (int t, int id, int v)
{
    int lo = 0, hi = 127;
    se_describe (isDrum (t) ? -1 : sounds[(size_t) t].engine, id, &lo, &hi, nullptr, nullptr);
    params[(size_t) t][(size_t) id] = juce::jlimit (lo, hi, v);
    audio.setParam (t, id, params[(size_t) t][(size_t) id]);
    notify();
}

void Controller::toggleSolo (int t)
{
    soloMask ^= 1u << t;
    audio.setSolo (soloMask);
    notify();
}
} // namespace sloop
