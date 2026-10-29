// SPDX-License-Identifier: GPL-3.0-only
// Projects: written and read back whole, older files migrated, a damaged one refused; the bundle (a project with its
// samples) back on another phone.
#include "model/Bundle.h"
#include "model/DawProject.h"
#include "sloop_engine.h"

namespace sloop
{
class ProjectTests : public juce::UnitTest
{
public:
    ProjectTests() : juce::UnitTest ("Projects") {}

    static Project sample()
    {
        Project p = Project::empty ("Test");
        p.bpm = 97;
        p.swing = 30;
        p.key = { 2, 3 };
        p.masterVolume = 90;
        p.globals[SE_G_DUST] = 12;
        auto c = Session::newClip (1, "Line");
        c.data.length = 24;
        c.data.setStep (4, 3, true);
        c.data.noteLen[3][4] = 5;
        c.data.notes[3].prob = 60;
        c.data.notes[3].cond = Cond::fill;
        c.data.setLock (3, 70, 33);
        p.session.at (1, 2) = c;
        auto d = Session::newClip (0, "Beat");
        d.data.lanes[2][7] = { 1, 70, 2, 100, -1, Cond::oneOfFour, 0 };
        p.session.at (0, 0) = d;
        p.sounds[3] = { 5, 1 };
        p.params[3][10] = 44;
        p.hasParams[3] = true;
        AudioClip a;
        a.name = "Loop";
        a.file = "loop.wav";
        a.start = 100;
        a.end = 44100;
        a.bpm = 93.5;
        a.stretch = false;
        p.session.audio[4] = a;
        p.audioTrack = true;
        p.tracks = 5;
        p.audioLevel = 70;
        p.audioPan = -20;
        return p;
    }

    void runTest() override
    {
        beginTest ("a project read back is the project written");
        {
            const auto p = sample();
            const auto back = Project::fromJson (p.toJson());
            expect (back.has_value());
            if (! back) return;
            expectEquals (back->bpm, 97);
            expectEquals (back->swing, 30);
            expect (back->key == p.key);
            expectEquals (back->globals.at (SE_G_DUST), 12);
            expectEquals (back->session.scenes(), p.session.scenes());
            const auto& c = back->session.at (1, 2);
            expect (c.has_value());
            if (c)
            {
                expectEquals ((int) c->data.length, 24);
                expectEquals (c->data.noteLength (3, 4), 5);
                expectEquals ((int) c->data.notes[3].prob, 60);
                expect (c->data.notes[3].cond == Cond::fill);
                expectEquals ((int) c->data.lock (3, 70), 33);
            }
            const auto& d = back->session.at (0, 0);
            expect (d.has_value() && d->data.lanes[2][7].ratchet == 2 && d->data.lanes[2][7].nudge == -1
                    && d->data.lanes[2][7].cond == Cond::oneOfFour);
            expectEquals (back->sounds[3].engine, 5);
            expectEquals (back->params[3][10], 44);
            const auto& a = back->session.audio[4];
            expect (a.has_value() && a->file == "loop.wav" && a->start == 100 && a->end == 44100 && ! a->stretch);
            expectWithinAbsoluteError (a ? a->bpm : 0.0, 93.5, 1.0e-9);
            expectEquals (back->audioPan, -20);
            expectEquals (back->tracks, 5);
            expect (back->audioTrack);
        }

        beginTest ("an older file: its engine parameters moved past the MOD stage");
        {
            auto p = sample();
            auto json = juce::JSON::parse (p.toJson());
            json.getDynamicObject()->setProperty ("format", 2);
            auto* tracks = json["tracks"].getArray();
            auto* params = (*tracks)[3]["params"].getArray();
            params->set (63, 111);                   // (format 2: the engine's first parameter was 63)
            const auto back = Project::fromJson (juce::JSON::toString (json));
            expect (back.has_value() && back->params[3][63 + 4] == 111);
        }

        beginTest ("an older file shows its tracks with clips, the audio track if it has some");
        {
            auto json = juce::JSON::parse (sample().toJson());
            json.getDynamicObject()->removeProperty ("trackCount");
            json.getDynamicObject()->removeProperty ("audioTrack");
            const auto back = Project::fromJson (juce::JSON::toString (json));
            expect (back && back->tracks == kMinTracks && back->audioTrack);
            auto p = sample();
            p.session.at (6, 0) = Session::newClip (6, "Late");
            p.tracks = 4;                            // (a track with clips is never out of sight)
            const auto shown = Project::fromJson (p.toJson());
            expect (shown && shown->tracks == 7);
        }

        beginTest ("a file that is not a project is refused");
        {
            expect (! Project::fromJson ("{}").has_value());
            expect (! Project::fromJson ("not json").has_value());
            auto json = juce::JSON::parse (sample().toJson());
            json.getDynamicObject()->setProperty ("format", 99);   // (a newer app's)
            expect (! Project::fromJson (juce::JSON::toString (json)).has_value());
        }

        beginTest ("a bundle carries the samples; on import they land in the folder, a clash renamed");
        {
            const auto from = juce::File::createTempFile ("from"), to = juce::File::createTempFile ("to");
            from.createDirectory();
            to.createDirectory();
            from.getChildFile ("loop.wav").replaceWithText ("the loop");
            to.getChildFile ("loop.wav").replaceWithText ("another sound");
            const auto data = bundle::pack (sample(), from);
            const auto back = bundle::unpack (data, to);
            expect (back.has_value());
            if (back && back->session.audio[4])
            {
                const auto file = back->session.audio[4]->file;
                expect (file != "loop.wav", "renamed: " + file);
                expectEquals (to.getChildFile (file).loadFileAsString(), juce::String ("the loop"));
                const auto again = bundle::unpack (data, to);   // (the same sample again: kept, not copied twice)
                expect (again && again->session.audio[4] && again->session.audio[4]->file == file);
            }
            const auto plain = sample().toJson();             // (a plain .sloop: JSON alone)
            expect (bundle::unpack (juce::MemoryBlock (plain.toRawUTF8(), plain.getNumBytesAsUTF8()), to).has_value());
            from.deleteRecursively();
            to.deleteRecursively();
        }

        beginTest ("the DAWproject: a track per track (the audio one when it has clips), every scene");
        {
            const auto xml = juce::XmlDocument::parse (dawproject::projectXml (sample()));
            expect (xml != nullptr);
            if (xml == nullptr) return;
            expectEquals (xml->getChildByName ("Structure")->getNumChildElements(), 5 + 2);   // (the tracks shown, AU, master)
            expectEquals (xml->getChildByName ("Scenes")->getNumChildElements(), sample().session.scenes());
        }
    }
};
static ProjectTests projectTests;
} // namespace sloop
