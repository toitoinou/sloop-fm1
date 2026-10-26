// SPDX-License-Identifier: GPL-3.0-only
#include "model/Samples.h"
#include "model/Project.h"

namespace sloop
{
SampleBank::SampleBank() { formats.registerBasicFormats(); }

juce::File SampleBank::folder()
{
    auto dir = store::folder().getChildFile ("samples");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> SampleBank::recordings()
{
    auto files = store::folder().getChildFile ("recordings").findChildFiles (juce::File::findFiles, false, "*.wav");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return files;
}

std::shared_ptr<const SampleData> SampleBank::get (const juce::String& file)
{
    if (file.isEmpty()) return nullptr;
    if (auto it = cache.find (file); it != cache.end()) return it->second;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (folder().getChildFile (file)));
    if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;
    auto s = std::make_shared<SampleData>();
    s->file = file;
    s->buffer.setSize (2, (int) reader->lengthInSamples);
    reader->read (&s->buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    if (reader->numChannels == 1) s->buffer.copyFrom (1, 0, s->buffer, 0, 0, s->buffer.getNumSamples());
    cache[file] = s;
    return s;
}

juce::String SampleBank::import (const juce::URL& from, juce::String& error)
{
    std::unique_ptr<juce::InputStream> in = from.isLocalFile()
        ? std::unique_ptr<juce::InputStream> (from.getLocalFile().createInputStream())
        : from.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
    std::unique_ptr<juce::AudioFormatReader> reader (in != nullptr ? formats.createReaderFor (std::move (in)) : nullptr);
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
    {
        error = juce::String::fromUTF8 ("Ce fichier n'est pas un son lisible (WAV, AIFF, FLAC, Ogg, MP3).");
        return {};
    }
    if ((double) reader->lengthInSamples / reader->sampleRate > kMaxSeconds)
    {
        error = juce::String::fromUTF8 ("Trop long : 10 minutes au plus.");
        return {};
    }
    // read it all, then to the engine's rate (a Lagrange interpolator: good enough for a loop)
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> src (2, n);
    reader->read (&src, 0, n, 0, true, true);
    if (reader->numChannels == 1) src.copyFrom (1, 0, src, 0, 0, n);
    const double ratio = reader->sampleRate / AudioClip::rate;
    juce::AudioBuffer<float> out (2, juce::jmax (1, (int) std::floor (n / ratio)));
    for (int ch = 0; ch < 2; ++ch)
    {
        if (std::abs (ratio - 1.0) < 1.0e-9) { out.copyFrom (ch, 0, src, ch, 0, juce::jmin (n, out.getNumSamples())); continue; }
        juce::LagrangeInterpolator interp;
        interp.process (ratio, src.getReadPointer (ch), out.getWritePointer (ch), out.getNumSamples(), n, 0);
    }
    // a name of its own in the folder
    auto base = juce::File::createLegalFileName (juce::URL::removeEscapeChars (from.getFileName())
                                                     .upToLastOccurrenceOf (".", false, false).trim());
    if (base.isEmpty()) base = "Sample";
    auto file = folder().getChildFile (base + ".wav");
    for (int i = 2; file.exists(); ++i) file = folder().getChildFile (base + " " + juce::String (i) + ".wav");
    auto stream = file.createOutputStream();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream.get(), AudioClip::rate, 2, 24, {}, 0) : nullptr);
    if (writer == nullptr)
    {
        error = juce::String::fromUTF8 ("Impossible d'écrire le sample dans le dossier de l'app.");
        return {};
    }
    stream.release();                                    // (the writer owns it)
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    writer.reset();
    auto s = std::make_shared<SampleData>();
    s->file = file.getFileName();
    s->buffer = std::move (out);
    cache[s->file] = s;
    return s->file;
}

void SampleBank::keepOnly (const std::set<juce::String>& files)
{
    std::vector<std::shared_ptr<const SampleData>> gone;
    for (auto it = cache.begin(); it != cache.end();)
        if (files.count (it->first) == 0) { gone.push_back (it->second); it = cache.erase (it); }
        else ++it;
    if (! gone.empty())                                  // (freed in 2 s, on this thread: never on the audio thread)
        juce::Timer::callAfterDelay (2000, [g = std::move (gone)] {});
}

double guessTempo (const juce::String& fileName, double seconds, double songTempo)
{
    const auto lower = fileName.toLowerCase();
    for (int i = lower.indexOf ("bpm"); i > 0; i = lower.indexOf (i + 1, "bpm"))
    {
        int k = i;
        while (k > 0 && (lower[k - 1] == ' ' || lower[k - 1] == '_' || lower[k - 1] == '-')) --k;   // ("92 bpm", "92_BPM")
        int j = k;
        while (j > 0 && juce::CharacterFunctions::isDigit (lower[j - 1])) --j;
        if (const int v = lower.substring (j, k).getIntValue(); j < k && v >= kMinBpm && v <= kMaxBpm) return v;
    }
    double best = songTempo, err = 1.0e9;
    for (int bars = 1; bars <= 32 && seconds > 0.0; bars *= 2)
    {
        const double t = bars * 4 * 60.0 / seconds;
        if (t >= 70.0 && t <= 180.0 && std::abs (t - songTempo) < err) { err = std::abs (t - songTempo); best = t; }
    }
    return best;
}
} // namespace sloop
