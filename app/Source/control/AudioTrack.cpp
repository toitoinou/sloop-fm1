// SPDX-License-Identifier: GPL-3.0-only
// The audio track: its clips (a sample each) launched, edited, imported.
#include "control/Controller.h"

namespace sloop
{
ClipPlayer::Clip Controller::playerClip (const AudioClip& c)
{
    ClipPlayer::Clip p;
    const auto data = samples.get (c.file);
    if (data == nullptr) return p;
    const int64_t n = data->buffer.getNumSamples();
    p.sample = data.get();                           // (the bank keeps it: the session holds its name)
    p.start = juce::jlimit ((int64_t) 0, n - 1, c.start);
    p.end = juce::jlimit (p.start + 1, n, c.end);
    p.beats = c.beats();
    p.stretch = c.stretch;
    p.gain = c.gain;
    return p;
}

void Controller::launchAudio (int s)
{
    if (s < 0 || s >= session.scenes() || ! session.audio[(size_t) s]) return;
    audioRow = s;
    audioQueued = playing ? s : kNone;
    audio.setAudioClip (playing ? AudioEngine::When::nextBar : AudioEngine::When::now, s, playerClip (*session.audio[(size_t) s]));
    notify();
}

void Controller::stopAudio()
{
    audioRow = -1;
    audioQueued = playing ? kStop : kNone;
    audio.setAudioClip (playing ? AudioEngine::When::nextBar : AudioEngine::When::now, -1, {});
}

// the clip as it is now, wherever it plays or waits
void Controller::audioEdited (int s)
{
    if (s >= 0 && s < session.scenes() && session.audio[(size_t) s])
        audio.setAudioClip (AudioEngine::When::update, s, playerClip (*session.audio[(size_t) s]));
    notify();
}

// an edit (the clip editor: a drag is one step back)
void Controller::setAudioClip (int s, const AudioClip& c)
{
    if (s < 0 || s >= session.scenes()) return;
    remember ("audio");
    session.audio[(size_t) s] = c;
    audioEdited (s);
}

void Controller::deleteAudio (int s)
{
    if (s < 0 || s >= session.scenes() || ! session.audio[(size_t) s]) return;
    remember ("clip");
    session.audio[(size_t) s].reset();
    if (audioRow == s) stopAudio();
    notify();
}

void Controller::duplicateAudio (int s)
{
    if (s < 0 || s >= session.scenes() || ! session.audio[(size_t) s]) return;
    remember ("clip");
    int r = s + 1;
    while (r < session.scenes() && session.audio[(size_t) r]) ++r;
    if (r == session.scenes()) session.addScene();
    session.audio[(size_t) r] = session.audio[(size_t) s];
    session.audio[(size_t) r]->name << " bis";
    notify();
}

bool Controller::importAudio (int s, const juce::URL& from, juce::String& error)
{
    const auto file = samples.import (from, error);
    if (file.isEmpty()) return false;
    const auto data = samples.get (file);
    AudioClip c;
    c.file = file;
    c.name = file.upToLastOccurrenceOf (".", false, false);
    c.end = data->buffer.getNumSamples();
    c.bpm = guessTempo (juce::URL::removeEscapeChars (from.getFileName()), c.seconds(), bpm);
    while (s >= session.scenes()) session.addScene();
    remember ("clip");
    session.audio[(size_t) s] = c;
    audioShown = true;                                   // (a sound in: the audio track shows)
    notify();
    return true;
}
} // namespace sloop
