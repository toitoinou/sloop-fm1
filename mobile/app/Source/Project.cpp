// SPDX-License-Identifier: GPL-3.0-only
#include "Project.h"

namespace sloop
{
namespace
{
constexpr int kFormat = 3;                      // the file's version: a later app reads older files
                                                // (2: clips up to 64 steps, 24 rows, the key, the swing;
                                                //  3: the MOD stage's 4 parameters before the engine's)
constexpr int kOldE0 = 63, kModParams = 4;       // (before format 3 the engine's parameters began at 63)
int paramId (int id, int format) { return format < 3 && id >= kOldE0 ? id + kModParams : id; }

juce::var stepVar (int a, int b, const StepData& x)
{
    // [index, lane or rows, velocity, ratchet, probability, nudge, condition]
    return juce::Array<juce::var> { a, b, (int) x.vel, (int) x.ratchet, (int) x.prob, (int) x.nudge, (int) x.cond };
}
void readStep (const juce::var& v, StepData& x)
{
    x.vel = (uint8_t) juce::jlimit (1, 127, (int) v[2]);
    x.ratchet = (uint8_t) juce::jlimit (1, 4, (int) v[3]);
    x.prob = (uint8_t) juce::jlimit (0, 100, (int) v[4]);
    x.nudge = (int8_t) juce::jlimit (-3, 3, (int) v[5]);
    x.cond = (Cond) juce::jlimit (0, (int) Cond::notFill, (int) v[6]);
}

juce::var clipVar (const Clip& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", c.name);
    o->setProperty ("length", (int) c.data.length);
    juce::Array<juce::var> steps, locks;
    for (int s = 0; s < c.data.length; ++s)
    {
        if (c.data.drum)
        {
            for (int l = 0; l < kLanes; ++l)
                if (c.data.lanes[(size_t) l][(size_t) s].on) steps.add (stepVar (s, l, c.data.lanes[(size_t) l][(size_t) s]));
        }
        else if (c.data.notes[(size_t) s].rows != 0)
        {
            auto v = stepVar (s, (int) c.data.notes[(size_t) s].rows, c.data.notes[(size_t) s]);
            juce::Array<juce::var> lens;                  // [row, length] of the notes longer than a step
            for (int r = 0; r < kRows; ++r)
                if ((c.data.notes[(size_t) s].rows >> r & 1) && c.data.noteLength (s, r) > 1)
                    lens.add (juce::Array<juce::var> { r, c.data.noteLength (s, r) });
            if (! lens.isEmpty()) v.append (lens);
            steps.add (v);
        }
    }
    for (int i = 0; i < c.data.nLocks; ++i)
    {
        const auto& l = c.data.locks[(size_t) i];
        locks.add (juce::Array<juce::var> { (int) l.step, (int) l.param, (int) l.value });
    }
    o->setProperty ("steps", steps);
    o->setProperty ("locks", locks);
    return juce::var (o);
}

std::optional<Clip> readClip (const juce::var& v, int track, int format)
{
    if (! v.isObject()) return std::nullopt;
    Clip c = Session::newClip (track, v["name"].toString());
    c.data.length = (uint8_t) juce::jlimit (1, kMaxSteps, (int) v.getProperty ("length", kBar));
    if (auto* steps = v["steps"].getArray())
        for (auto& st : *steps)
        {
            if (! st.isArray() || st.size() < 7) continue;     // (a damaged file: what can be read is kept)
            const int s = (int) st[0], b = (int) st[1];
            if (s < 0 || s >= c.data.length) continue;
            if (c.data.drum)
            {
                if (b < 0 || b >= kLanes) continue;
                auto& x = c.data.lanes[(size_t) b][(size_t) s];
                x.on = 1;
                readStep (st, x);
            }
            else
            {
                auto& x = c.data.notes[(size_t) s];
                x.rows = (uint32_t) b & ((1u << kRows) - 1u);
                x.on = x.rows != 0;
                readStep (st, x);
                for (int r = 0; r < kRows; ++r) if (x.rows >> r & 1) c.data.noteLen[(size_t) s][(size_t) r] = 1;
                if (auto* lens = st.size() > 7 ? st[7].getArray() : nullptr)
                    for (auto& l : *lens)
                        if (l.isArray() && l.size() >= 2 && (int) l[0] >= 0 && (int) l[0] < kRows)
                            c.data.noteLen[(size_t) s][(size_t) (int) l[0]] = (uint8_t) juce::jlimit (1, kMaxSteps - s, (int) l[1]);
            }
        }
    if (auto* locks = v["locks"].getArray())
        for (auto& l : *locks)
            if (l.isArray() && l.size() >= 3 && (int) l[0] >= 0 && (int) l[0] < kMaxSteps && (int) l[1] >= 0
                && paramId ((int) l[1], format) < kParamSlots)
                c.data.setLock ((int) l[0], paramId ((int) l[1], format), (int) l[2]);
    return c;
}
} // namespace

juce::String Project::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "SLOOP Touch");
    root->setProperty ("format", kFormat);
    root->setProperty ("name", name);
    root->setProperty ("bpm", bpm);
    root->setProperty ("swing", swing);
    root->setProperty ("root", key.root);
    root->setProperty ("scale", key.scale);
    juce::Array<juce::var> tracks;
    for (int t = 0; t < kTracks; ++t)
    {
        auto* tr = new juce::DynamicObject();
        tr->setProperty ("engine", sounds[(size_t) t].engine);
        tr->setProperty ("preset", sounds[(size_t) t].preset);
        if (hasParams[(size_t) t])
        {
            juce::Array<juce::var> p;
            for (int v : params[(size_t) t]) p.add (v);
            tr->setProperty ("params", p);
        }
        juce::Array<juce::var> clips;
        for (auto& c : session.cols[(size_t) t]) clips.add (c ? clipVar (*c) : juce::var());
        tr->setProperty ("clips", clips);
        tracks.add (juce::var (tr));
    }
    root->setProperty ("tracks", tracks);
    return juce::JSON::toString (juce::var (root), true);
}

std::optional<Project> Project::fromJson (const juce::String& text)
{
    const auto root = juce::JSON::parse (text);
    auto* tracks = root["tracks"].getArray();
    if (! root.isObject() || tracks == nullptr || tracks->isEmpty() || tracks->size() > kTracks || (int) root["format"] > kFormat)
        return std::nullopt;
    Project p;
    const int format = (int) root["format"];
    p.name = root["name"].toString();
    p.bpm = juce::jlimit (60, 200, (int) root.getProperty ("bpm", 118));
    p.swing = juce::jlimit (0, 100, (int) root.getProperty ("swing", 0));
    p.key.root = juce::jlimit (0, 11, (int) root.getProperty ("root", 0));
    p.key.scale = juce::jlimit (0, (int) scales().size() - 1, (int) root.getProperty ("scale", 0));
    int scenes = 1;
    for (auto& tr : *tracks)
        if (auto* clips = tr["clips"].getArray()) scenes = juce::jmax (scenes, clips->size());
    for (int i = 0; i < scenes; ++i) p.session.addScene();
    for (int t = 0; t < tracks->size(); ++t)            // (an older project: four tracks, the others as new)
    {
        const auto& tr = (*tracks)[t];
        p.sounds[(size_t) t] = { (int) tr.getProperty ("engine", kDefaultSounds[(size_t) t].engine),
                                 (int) tr.getProperty ("preset", kDefaultSounds[(size_t) t].preset) };
        if (auto* params = tr["params"].getArray())
        {
            for (int i = 0; i < params->size(); ++i)
                if (const int id = paramId (i, format); id < kParamSlots) p.params[(size_t) t][(size_t) id] = (*params)[i];
            p.hasParams[(size_t) t] = true;
        }
        if (auto* clips = tr["clips"].getArray())
            for (int s = 0; s < clips->size(); ++s) p.session.at (t, s) = readClip ((*clips)[s], t, format);
    }
    return p;
}

Project Project::empty (const juce::String& name)
{
    Project p;
    p.name = name;
    for (int i = 0; i < 6; ++i) p.session.addScene();
    return p;
}

namespace store
{
juce::File folder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SLOOP Touch");
    dir.getChildFile ("projects").createDirectory();
    return dir;
}
juce::File autosaveFile() { return folder().getChildFile ("autosave.sloop"); }
juce::Array<juce::File> projects()
{
    auto files = folder().getChildFile ("projects").findChildFiles (juce::File::findFiles, false, "*.sloop");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return files;
}
juce::File fileFor (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) safe = "Projet";
    return folder().getChildFile ("projects").getChildFile (safe + ".sloop");
}
bool write (const juce::File& f, const Project& p)
{
    // a temporary file, then a rename: a crash in the middle never leaves half a project
    juce::TemporaryFile tmp (f);
    return tmp.getFile().replaceWithText (p.toJson()) && tmp.overwriteTargetFileWithTemporary();
}
std::optional<Project> read (const juce::File& f)
{
    return f.existsAsFile() ? Project::fromJson (f.loadFileAsString()) : std::nullopt;
}
Prefs readPrefs()
{
    const auto v = juce::JSON::parse (folder().getChildFile ("prefs.json").loadFileAsString());
    Prefs p;
    p.metronome = v.getProperty ("metronome", p.metronome);
    p.countIn = v.getProperty ("countIn", p.countIn);
    p.quantizeRec = v.getProperty ("quantizeRec", p.quantizeRec);
    return p;
}
void writePrefs (const Prefs& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("metronome", p.metronome);
    o->setProperty ("countIn", p.countIn);
    o->setProperty ("quantizeRec", p.quantizeRec);
    folder().getChildFile ("prefs.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}
} // namespace store
} // namespace sloop
