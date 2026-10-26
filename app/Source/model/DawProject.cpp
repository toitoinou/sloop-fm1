// SPDX-License-Identifier: GPL-3.0-only
#include "model/DawProject.h"
#include "model/Samples.h"
#include "sloop_engine.h"

namespace sloop::dawproject
{
namespace
{
constexpr double kStepBeats = 0.25;              // a 1/16 step


struct Ids
{
    int next = 0;
    juce::String operator()() { return "id" + juce::String (next++); }
};

juce::String num (double v) { return juce::String (v, 6).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); }

juce::XmlElement* add (juce::XmlElement& parent, const char* tag, Ids* ids = nullptr)
{
    auto* e = parent.createNewChildElement (tag);
    if (ids != nullptr) e->setAttribute ("id", (*ids)());
    return e;
}

// the notes of one pass of a clip from `at` (beats), as the app would play it with no fill held
void addNotes (juce::XmlElement& notes, const ClipData& d, int track, const Key& key, double at)
{
    auto note = [&notes, at] (double time, double duration, int midi, int vel)
    {
        auto* n = notes.createNewChildElement ("Note");
        n->setAttribute ("time", num (at + juce::jmax (0.0, time)));
        n->setAttribute ("duration", num (duration));
        n->setAttribute ("channel", 0);
        n->setAttribute ("key", juce::jlimit (0, 127, midi));
        n->setAttribute ("vel", num (vel / 127.0));
        n->setAttribute ("rel", num (vel / 127.0));
    };
    // a step: on its place (and its nudge, sixths of a step), its ratchets splitting it
    auto step = [&note] (const StepData& x, int s, double length, int midi)
    {
        const double t0 = (s + x.nudge / 6.0) * kStepBeats;
        const int r = x.ratchet > 1 && length <= kStepBeats ? x.ratchet : 1;
        for (int k = 0; k < r; ++k) note (t0 + k * length / r, length / r, midi, x.vel);
    };
    for (int s = 0; s < d.length; ++s)
    {
        if (d.drum)
        {
            for (int lane = 0; lane < kLanes; ++lane)
                if (const auto& x = d.lanes[(size_t) lane][(size_t) s]; x.on && x.cond != Cond::fill)
                    step (x, s, kStepBeats, se_lane_note (lane));
        }
        else if (const auto& x = d.notes[(size_t) s]; x.rows != 0 && x.cond != Cond::fill)
        {
            for (int r = 0; r < kRows; ++r)
                if (x.rows >> r & 1u)
                    step (x, s, juce::jmin (d.noteLength (s, r), d.length - s) * kStepBeats, rowNote (track, r, key));
        }
    }
}

juce::String colourOf (int t) { return "#" + trackColour (t).toDisplayString (false).toLowerCase(); }

bool hasAudio (const Project& p)
{
    return std::any_of (p.session.audio.begin(), p.session.audio.end(), [] (const auto& c) { return c.has_value(); });
}

// an audio clip's content: its sample (audio/<file> in the zip), its start and end on its beats (the DAW stretches it
// to the song's tempo as the app does)
void addWarps (juce::XmlElement& clip, const AudioClip& c, Ids& ids)
{
    auto* warps = add (clip, "Warps", &ids);
    warps->setAttribute ("timeUnit", "beats");
    warps->setAttribute ("contentTimeUnit", "seconds");
    auto* audio = add (*warps, "Audio", &ids);
    audio->setAttribute ("algorithm", c.stretch ? "stretch" : "repitch");
    audio->setAttribute ("channels", 2);
    audio->setAttribute ("sampleRate", (int) AudioClip::rate);
    const auto file = SampleBank::folder().getChildFile (c.file);
    double seconds = (double) c.end / AudioClip::rate;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file)); r != nullptr)
        seconds = (double) r->lengthInSamples / r->sampleRate;
    audio->setAttribute ("duration", num (seconds));
    add (*audio, "File")->setAttribute ("path", "audio/" + c.file);
    auto* w0 = add (*warps, "Warp");
    w0->setAttribute ("time", "0");
    w0->setAttribute ("contentTime", num ((double) c.start / AudioClip::rate));
    auto* w1 = add (*warps, "Warp");
    w1->setAttribute ("time", num (c.beats()));
    w1->setAttribute ("contentTime", num ((double) c.end / AudioClip::rate));
}
} // namespace

juce::String projectXml (const Project& p)
{
    Ids ids;
    juce::XmlElement root ("Project");
    root.setAttribute ("version", "1.0");
    auto* app = add (root, "Application");
    app->setAttribute ("name", "SLOOP Touch");
    app->setAttribute ("version", juce::JUCEApplicationBase::getInstance() != nullptr
                                      ? juce::JUCEApplicationBase::getInstance()->getApplicationVersion() : juce::String ("0.1"));
    auto* transport = add (root, "Transport");
    auto* tempo = add (*transport, "Tempo", &ids);
    tempo->setAttribute ("name", "Tempo");
    tempo->setAttribute ("unit", "bpm");
    tempo->setAttribute ("value", num (p.bpm));
    auto* sig = add (*transport, "TimeSignature", &ids);
    sig->setAttribute ("numerator", 4);
    sig->setAttribute ("denominator", 4);

    // the tracks, and the master they go to
    auto* structure = add (root, "Structure");
    std::array<juce::String, kTracks> trackIds;
    std::array<juce::XmlElement*, kTracks> channels {};
    for (int t = 0; t < kTracks; ++t)
    {
        auto* tr = add (*structure, "Track", &ids);
        trackIds[(size_t) t] = tr->getStringAttribute ("id");
        const auto& snd = p.sounds[(size_t) t];
        tr->setAttribute ("name", juce::String (trackName (t)) + " (" + (isDrum (t) ? se_kit_name (snd.preset)
                                                                               : se_preset_name (snd.engine, snd.preset)) + ")");
        tr->setAttribute ("color", colourOf (t));
        tr->setAttribute ("contentType", "notes");
        tr->setAttribute ("loaded", "true");
        channels[(size_t) t] = add (*tr, "Channel", &ids);
    }
    juce::String audioTrackId;
    juce::XmlElement* audioChannel = nullptr;
    if (hasAudio (p))                                    // (the audio track: only when it has clips)
    {
        auto* tr = add (*structure, "Track", &ids);
        audioTrackId = tr->getStringAttribute ("id");
        tr->setAttribute ("name", "Audio");
        tr->setAttribute ("color", colourOf (kAudio));
        tr->setAttribute ("contentType", "audio");
        tr->setAttribute ("loaded", "true");
        audioChannel = add (*tr, "Channel", &ids);
    }
    auto* masterTrack = add (*structure, "Track", &ids);
    masterTrack->setAttribute ("name", "Master");
    masterTrack->setAttribute ("contentType", "audio notes");
    masterTrack->setAttribute ("loaded", "true");
    auto* master = add (*masterTrack, "Channel", &ids);
    const auto masterId = master->getStringAttribute ("id");
    auto mixer = [&ids] (juce::XmlElement& ch, const juce::String& role, bool muted, double pan)
    {
        ch.setAttribute ("audioChannels", 2);
        ch.setAttribute ("role", role);
        ch.setAttribute ("solo", "false");
        auto* m = add (ch, "Mute", &ids);
        m->setAttribute ("name", "Mute");
        m->setAttribute ("value", muted ? "true" : "false");
        auto* pn = add (ch, "Pan", &ids);
        pn->setAttribute ("name", "Pan");
        pn->setAttribute ("unit", "normalized");
        pn->setAttribute ("min", "0");
        pn->setAttribute ("max", "1");
        pn->setAttribute ("value", num (pan));
        auto* v = add (ch, "Volume", &ids);
        v->setAttribute ("name", "Volume");
        v->setAttribute ("unit", "linear");
        v->setAttribute ("min", "0");
        v->setAttribute ("max", "2");
        v->setAttribute ("value", "1");
    };
    for (int t = 0; t < kTracks; ++t)
    {
        const int pan = p.hasParams[(size_t) t] ? p.params[(size_t) t][(size_t) SE_P_PAN] : 0;      // SLOOP's PAN: -64..63
        channels[(size_t) t]->setAttribute ("destination", masterId);
        mixer (*channels[(size_t) t], "regular", p.hasParams[(size_t) t] && p.params[(size_t) t][(size_t) SE_P_MUTE] != 0,
               juce::jlimit (0.0, 1.0, 0.5 + pan / 128.0));
    }
    if (audioChannel != nullptr)
    {
        audioChannel->setAttribute ("destination", masterId);
        mixer (*audioChannel, "regular", p.audioMuted, juce::jlimit (0.0, 1.0, 0.5 + p.audioPan / 128.0));
    }
    mixer (*master, "master", false, 0.5);

    // the arrangement: the scenes in order, each as long as its longest clip; the shorter clips loop in it
    auto* arrangement = add (root, "Arrangement", &ids);
    auto* lanes = add (*arrangement, "Lanes", &ids);
    lanes->setAttribute ("timeUnit", "beats");
    std::array<juce::XmlElement*, kTracks> clipsOf {};
    for (int t = 0; t < kTracks; ++t)
    {
        auto* trackLane = add (*lanes, "Lanes", &ids);
        trackLane->setAttribute ("track", trackIds[(size_t) t]);
        clipsOf[(size_t) t] = add (*trackLane, "Clips", &ids);
    }
    juce::XmlElement* audioClips = nullptr;
    if (audioChannel != nullptr)
    {
        auto* trackLane = add (*lanes, "Lanes", &ids);
        trackLane->setAttribute ("track", audioTrackId);
        audioClips = add (*trackLane, "Clips", &ids);
    }
    double at = 0;
    for (int r = 0; r < p.session.scenes(); ++r)
    {
        int steps = 0;
        for (int t = 0; t < kTracks; ++t)
            if (const auto& c = p.session.at (t, r)) steps = juce::jmax (steps, (int) c->data.length);
        const auto& ac = p.session.audio[(size_t) r];
        if (ac) steps = juce::jmax (steps, (int) (ac->beats() / kStepBeats));
        if (steps == 0) continue;                    // (an empty row: nothing to play)
        const double beats = steps * kStepBeats;
        if (ac && audioClips != nullptr)
        {
            auto* clip = add (*audioClips, "Clip");
            clip->setAttribute ("name", ac->name);
            clip->setAttribute ("time", num (at));
            clip->setAttribute ("duration", num (beats));
            clip->setAttribute ("playStart", "0");
            clip->setAttribute ("loopStart", "0");
            clip->setAttribute ("loopEnd", num (ac->beats()));
            addWarps (*clip, *ac, ids);
        }
        for (int t = 0; t < kTracks; ++t)
        {
            const auto& c = p.session.at (t, r);
            if (! c) continue;
            auto* clip = add (*clipsOf[(size_t) t], "Clip");
            clip->setAttribute ("name", c->name);
            clip->setAttribute ("time", num (at));
            clip->setAttribute ("duration", num (beats));
            clip->setAttribute ("playStart", "0");
            auto* notes = add (*clip, "Notes", &ids);
            const double len = c->data.length * kStepBeats;
            for (double pass = 0; pass < beats - 1e-9; pass += len)
            {
                juce::XmlElement one ("Notes");
                addNotes (one, c->data, t, p.key, pass);
                for (auto* n : one.getChildIterator())
                    if (n->getDoubleAttribute ("time") < beats - 1e-9) notes->addChildElement (new juce::XmlElement (*n));
            }
        }
        at += beats;
    }

    // the scenes: the clip launcher's rows, each clip looping on its own length
    auto* scenes = add (root, "Scenes");
    for (int r = 0; r < p.session.scenes(); ++r)
    {
        auto* scene = add (*scenes, "Scene", &ids);
        scene->setAttribute ("name", juce::String::fromUTF8 ("Scène ") + juce::String (r + 1));
        auto* row = add (*scene, "Lanes", &ids);
        row->setAttribute ("timeUnit", "beats");
        for (int t = 0; t < kTracks; ++t)
        {
            auto* slot = add (*row, "ClipSlot", &ids);
            slot->setAttribute ("track", trackIds[(size_t) t]);
            slot->setAttribute ("hasStop", "true");
            const auto& c = p.session.at (t, r);
            if (! c) continue;
            const double len = c->data.length * kStepBeats;
            auto* clip = add (*slot, "Clip");
            clip->setAttribute ("name", c->name);
            clip->setAttribute ("color", colourOf (t));
            clip->setAttribute ("time", "0");
            clip->setAttribute ("duration", num (len));
            clip->setAttribute ("playStart", "0");
            clip->setAttribute ("loopStart", "0");
            clip->setAttribute ("loopEnd", num (len));
            addNotes (*add (*clip, "Notes", &ids), c->data, t, p.key, 0.0);
        }
        if (audioChannel != nullptr)
        {
            auto* slot = add (*row, "ClipSlot", &ids);
            slot->setAttribute ("track", audioTrackId);
            slot->setAttribute ("hasStop", "true");
            if (const auto& ac = p.session.audio[(size_t) r])
            {
                auto* clip = add (*slot, "Clip");
                clip->setAttribute ("name", ac->name);
                clip->setAttribute ("color", colourOf (kAudio));
                clip->setAttribute ("time", "0");
                clip->setAttribute ("duration", num (ac->beats()));
                clip->setAttribute ("playStart", "0");
                clip->setAttribute ("loopStart", "0");
                clip->setAttribute ("loopEnd", num (ac->beats()));
                addWarps (*clip, *ac, ids);
            }
        }
    }
    return root.toString();
}

juce::String metadataXml (const Project& p)
{
    juce::XmlElement root ("MetaData");
    root.createNewChildElement ("Title")->addTextElement (p.name.isNotEmpty() ? p.name : juce::String ("SLOOP Touch"));
    root.createNewChildElement ("Comment")->addTextElement (
        juce::String::fromUTF8 ("Exporté de SLOOP Touch. Tonalité : ") + pitchName (p.key.root) + " "
        + juce::String::fromUTF8 (p.key.def().name) + ", swing " + juce::String (50.0 + p.swing / 4.0, 1) + " %");
    return root.toString();
}

juce::MemoryBlock zip (const Project& p)
{
    juce::ZipFile::Builder builder;
    auto entry = [&builder] (const juce::String& text, const char* name)
    {
        auto* in = new juce::MemoryInputStream (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()), true);
        builder.addEntry (in, 9, name, juce::Time::getCurrentTime());
    };
    entry (projectXml (p), "project.xml");
    entry (metadataXml (p), "metadata.xml");
    std::set<juce::String> files;                        // the audio clips' samples, once each
    for (auto& c : p.session.audio) if (c) files.insert (c->file);
    for (auto& f : files)
        if (const auto file = SampleBank::folder().getChildFile (f); file.existsAsFile())
            builder.addFile (file, 0, "audio/" + f);     // (WAV: stored, not compressed)
    juce::MemoryOutputStream out;
    builder.writeToStream (out, nullptr);
    return out.getMemoryBlock();
}
} // namespace sloop::dawproject
