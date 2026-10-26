// SPDX-License-Identifier: GPL-3.0-only
// The session as a DAWproject file (github.com/bitwig/dawproject: Bitwig Studio, Studio One, Cubase, Reaper with an
// extension...), to finish a song in a DAW. One MIDI track per track; the scenes go into the DAW's clip launcher
// (where it has one), and also one after the other into the arrangement, each as long as its longest clip.
// The notes as written: the probabilities and the 1:2 / 1:4 conditions all play (a DAW has no such thing), the
// fill-only steps are left out; no sound (the DAW's instruments play the notes: the drums on the General MIDI
// notes, kick 36, snare 38...). The audio track's clips: their samples in the zip (audio/), warped on their beats.
#pragma once
#include "model/Project.h"

namespace sloop::dawproject
{
juce::String projectXml (const Project&);
juce::String metadataXml (const Project&);
juce::MemoryBlock zip (const Project&);          // the .dawproject file: the two above in a zip, the samples
} // namespace sloop::dawproject
