// SPDX-License-Identifier: GPL-3.0-only
#include "model/Bundle.h"
#include <set>

namespace sloop::bundle
{
juce::MemoryBlock pack (const Project& p, const juce::File& samplesFolder)
{
    juce::ZipFile::Builder zip;
    const auto json = p.toJson();
    zip.addEntry (new juce::MemoryInputStream (juce::MemoryBlock (json.toRawUTF8(), json.getNumBytesAsUTF8()), true), 9,
                  "project.json", juce::Time::getCurrentTime());
    std::set<juce::String> files;
    for (auto& c : p.session.audio)
        if (c) files.insert (c->file);
    for (auto& f : files)
        if (const auto file = samplesFolder.getChildFile (f); file.existsAsFile())
            zip.addFile (file, 0, "samples/" + f);       // (WAV: stored as it is)
    juce::MemoryOutputStream out;
    zip.writeToStream (out, nullptr);
    return out.getMemoryBlock();
}

std::optional<Project> unpack (const juce::MemoryBlock& data, const juce::File& samplesFolder)
{
    const bool isZip = data.getSize() > 4 && std::memcmp (data.getData(), "PK\x03\x04", 4) == 0;
    if (! isZip) return Project::fromJson (data.toString());
    juce::MemoryInputStream in (data, false);
    juce::ZipFile zip (in);
    std::unique_ptr<juce::InputStream> json (zip.createStreamForEntry (zip.getIndexOfFileName ("project.json")));
    if (json == nullptr) return std::nullopt;
    auto p = Project::fromJson (json->readEntireStreamAsString());
    if (! p) return std::nullopt;
    samplesFolder.createDirectory();
    std::map<juce::String, juce::String> renamed;        // the bundle's name -> the folder's
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        if (e == nullptr || ! e->filename.startsWith ("samples/")) continue;
        const auto name = juce::File::createLegalFileName (e->filename.fromFirstOccurrenceOf ("samples/", false, false));
        if (name.isEmpty()) continue;
        juce::MemoryBlock bytes;
        if (std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i)); s != nullptr) s->readIntoMemoryBlock (bytes);
        auto target = samplesFolder.getChildFile (name);
        for (int k = 2; target.existsAsFile(); ++k)       // the same sample: kept; another one: a name of its own
        {
            juce::MemoryBlock there;
            if (target.loadFileAsData (there) && there == bytes) break;
            target = samplesFolder.getChildFile (name.upToLastOccurrenceOf (".", false, false) + " " + juce::String (k)
                                                 + name.fromLastOccurrenceOf (".", true, false));
        }
        if (! target.existsAsFile() && ! target.replaceWithData (bytes.getData(), bytes.getSize())) return std::nullopt;
        renamed[name] = target.getFileName();
    }
    for (auto& c : p->session.audio)
        if (c)
            if (auto it = renamed.find (c->file); it != renamed.end()) c->file = it->second;
    return p;
}
} // namespace sloop::bundle
