// SPDX-License-Identifier: GPL-3.0-only
// A project to share (Projet > Exporter / Importer): a zip holding project.json and, under samples/, every sample
// its audio clips play, so the file is whole on another phone. Importing reads it back (a plain .sloop, JSON alone,
// too): the samples go into the app's samples folder (one there already with the same name and other sounds: the
// new one gets a name of its own, the clips follow it).
#pragma once
#include "model/Project.h"

namespace sloop::bundle
{
juce::MemoryBlock pack (const Project&, const juce::File& samplesFolder);
std::optional<Project> unpack (const juce::MemoryBlock&, const juce::File& samplesFolder);
} // namespace sloop::bundle
