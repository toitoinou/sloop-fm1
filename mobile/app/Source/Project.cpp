// SPDX-License-Identifier: GPL-3.0-only
#include "Project.h"

namespace sloop
{
namespace
{
constexpr int kFormat = 1;                      // the file's version: a later app reads older files

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
    for (int s = 0; s < kSteps; ++s)
    {
        if (c.data.drum)
        {
            for (int l = 0; l < kLanes; ++l)
                if (c.data.lanes[(size_t) l][(size_t) s].on) steps.add (stepVar (s, l, c.data.lanes[(size_t) l][(size_t) s]));
        }
        else if (c.data.notes[(size_t) s].rows != 0)
            steps.add (stepVar (s, c.data.notes[(size_t) s].rows, c.data.notes[(size_t) s]));
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

std::optional<Clip> readClip (const juce::var& v, int track)
{
    if (! v.isObject()) return std::nullopt;
    Clip c = Session::newClip (track, v["name"].toString());
    c.data.length = (uint8_t) juce::jlimit (1, kSteps, (int) v.getProperty ("length", kSteps));
    if (auto* steps = v["steps"].getArray())
        for (auto& st : *steps)
        {
            const int s = (int) st[0], b = (int) st[1];
            if (s < 0 || s >= kSteps) continue;
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
                x.rows = (uint16_t) (b & 0xFFFF);
                x.on = x.rows != 0;
                readStep (st, x);
            }
        }
    if (auto* locks = v["locks"].getArray())
        for (auto& l : *locks)
            if ((int) l[0] >= 0 && (int) l[0] < kSteps && (int) l[1] >= 0 && (int) l[1] < kParamSlots)
                c.data.setLock ((int) l[0], (int) l[1], (int) l[2]);
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
    juce::Array<juce::var> tracks;
    for (int t = 0; t < kTracks; ++t)
    {
        auto* tr = new juce::DynamicObject();
        tr->setProperty ("engine", sounds[(size_t) t].engine);
        tr->setProperty ("preset", sounds[(size_t) t].preset);
        if (hasParams)
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
    if (! root.isObject() || tracks == nullptr || tracks->size() != kTracks || (int) root["format"] > kFormat)
        return std::nullopt;
    Project p;
    p.name = root["name"].toString();
    p.bpm = juce::jlimit (60, 200, (int) root.getProperty ("bpm", 118));
    int scenes = 1;
    for (auto& tr : *tracks)
        if (auto* clips = tr["clips"].getArray()) scenes = juce::jmax (scenes, clips->size());
    for (int i = 0; i < scenes; ++i) p.session.addScene();
    p.hasParams = true;
    for (int t = 0; t < kTracks; ++t)
    {
        const auto& tr = (*tracks)[t];
        p.sounds[(size_t) t] = { (int) tr.getProperty ("engine", kDefaultSounds[(size_t) t].engine),
                                 (int) tr.getProperty ("preset", kDefaultSounds[(size_t) t].preset) };
        if (auto* params = tr["params"].getArray())
            for (int i = 0; i < juce::jmin (kParamSlots, params->size()); ++i) p.params[(size_t) t][(size_t) i] = (*params)[i];
        else
            p.hasParams = false;
        if (auto* clips = tr["clips"].getArray())
            for (int s = 0; s < clips->size(); ++s) p.session.at (t, s) = readClip ((*clips)[s], t);
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
} // namespace store
} // namespace sloop
