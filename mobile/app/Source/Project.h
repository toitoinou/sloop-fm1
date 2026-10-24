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
    Session session;
    std::array<Sound, kTracks> sounds = kDefaultSounds;
    std::array<std::array<int, kParamSlots>, kTracks> params {};
    bool hasParams = false;                      // (a new project: the sounds' own values)

    juce::String toJson() const;
    static std::optional<Project> fromJson (const juce::String&);
    static Project empty (const juce::String& name);
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
} // namespace store
} // namespace sloop
