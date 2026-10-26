// SPDX-License-Identifier: GPL-3.0-only
// A project: the session (clips, scenes), the tempo, each track's sound and every one of its parameters, as one
// JSON file (.sloop). Kept in the app's own storage (named projects, and an autosave the app reopens), or exported
// and imported as a file.
#pragma once
#include "Model.h"

namespace sloop
{
struct Project
{
    juce::String name;
    int bpm = 118;
    int swing = 0;
    Key key;
    Session session;
    std::array<Sound, kTracks> sounds = kDefaultSounds;
    std::array<std::array<int, kParamSlots>, kTracks> params {};
    std::array<bool, kTracks> hasParams {};      // a track's saved values (a new project, a track not saved: the sound's)

    juce::String toJson() const;
    static std::optional<Project> fromJson (const juce::String&);
    static Project empty (const juce::String& name);
};

// the app's own settings (not a project's): kept apart, in prefs.json
struct Prefs
{
    bool metronome = false;
    bool countIn = true;                         // REC from stopped: a bar of clicks first
    bool quantizeRec = true;                     // a hit recorded on the nearest step (else between steps)
};

// the projects kept in the app (userApplicationDataDirectory/SLOOP Touch/projects/*.sloop)
namespace store
{
juce::File folder();
juce::File autosaveFile();
juce::Array<juce::File> projects();                    // the newest first
juce::File fileFor (const juce::String& name);
bool write (const juce::File&, const Project&);
std::optional<Project> read (const juce::File&);
Prefs readPrefs();
void writePrefs (const Prefs&);
} // namespace store
} // namespace sloop
