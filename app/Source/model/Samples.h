// SPDX-License-Identifier: GPL-3.0-only
// The samples the audio clips play: files of the app's folder (SLOOP Touch/samples), WAV, stereo, at the engine's
// rate (44.1 kHz). Any file JUCE reads (WAV, AIFF, FLAC, Ogg, MP3) is converted to one when imported; the app's own
// recordings (SLOOP Touch/recordings) too. A sample is read into memory once and never changes after: the audio
// thread reads it.
#pragma once
#include "model/Model.h"
#include <map>
#include <memory>
#include <set>

namespace sloop
{
struct SampleData
{
    juce::AudioBuffer<float> buffer;                     // 2 channels, AudioClip::rate
    juce::String file;
};

class SampleBank
{
public:
    SampleBank();
    static juce::File folder();
    static juce::Array<juce::File> recordings();         // the app's recordings, the latest first

    std::shared_ptr<const SampleData> get (const juce::String& file);   // nullptr: no such file
    // a file, or what Android's file picker gives (a content:// URL): the new sample's file name, "" (and why) if not
    juce::String import (const juce::URL& from, juce::String& error);
    juce::String import (const juce::File& from, juce::String& error) { return import (juce::URL (from), error); }
    // the samples not listed are let go (a project loaded): a little later, the audio thread may still read them
    void keepOnly (const std::set<juce::String>& files);

    static constexpr double kMaxSeconds = 600.0;         // (10 minutes: 200 MB in memory)

private:
    juce::AudioFormatManager formats;
    std::map<juce::String, std::shared_ptr<const SampleData>> cache;
};
// a sample's tempo at first: from its name ("... 92bpm.wav", "92 BPM"), else the one nearest to the song's that
// makes its length 1, 2, 4... 32 bars (between 70 and 180 BPM); else the song's
double guessTempo (const juce::String& fileName, double seconds, double songTempo);
} // namespace sloop
