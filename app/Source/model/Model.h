// SPDX-License-Identifier: GPL-3.0-only
// The app's music: a session of clips (one column per track, one row per scene), as the mockup
// (mobile/mockup/sloop-touch.html). ClipData is plain data: the audio thread gets copies of it.
#pragma once
#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace sloop
{
// The app's tracks: DR (SLOOP's drum track) then T1..T3 (its synth parts 0..2)
constexpr int kTracks = 8;          // DR, then T1..T7
constexpr int kMaxSteps = 64;       // a clip's longest (SLOOP's NSTEP): 4 bars of 1/16
constexpr int kBar = 16;            // a bar's steps: a new clip's length, and where launched clips start
constexpr int kMinBpm = 40, kMaxBpm = 240;   // (SLOOP's BPM range)
constexpr int kLanes = 16;
constexpr int kRows = 24;           // the piano roll's rows: the degrees of the key's scale, from its root
constexpr int kKnobs = 4;
constexpr int kMaxLocks = 256;      // a clip's parameter locks (steps held, knob moves recorded; the engine gets a few steps' at a time)
constexpr int kParamSlots = 96;     // room for every SLOOP track parameter (se_param_count(): 83)
constexpr int kChainMax = 8;
constexpr int16_t kNoLock = INT16_MIN;

// note repeat's rates: 1/4 1/8 1/16 1/32 1/8T 1/16T, as steps (a 1/16 each)
inline double repeatSteps (int rate)
{
    static const double s[] = { 4.0, 2.0, 1.0, 0.5, 4.0 / 3.0, 2.0 / 3.0 };
    return s[juce::jlimit (0, 5, rate)];
}
inline const char* repeatName (int rate)
{
    static const char* n[] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
    return n[juce::jlimit (0, 5, rate)];
}
inline int engineTrack (int t) { return t == 0 ? kTracks - 1 : t - 1; }   // (SLOOP's drum track is its last)
inline const char* trackId (int t)
{
    static const char* ids[kTracks] = { "DR", "T1", "T2", "T3", "T4", "T5", "T6", "T7" };
    return ids[(size_t) t % kTracks];
}
inline const char* trackName (int t)
{
    static const char* names[kTracks] = { "Drums", "Bass", "Keys", "Lead", "Pad", "Pluck", "Arp", "Texture" };
    return names[(size_t) t % kTracks];
}
inline bool isDrum (int t) { return t == 0; }
inline juce::Colour trackColour (int t)                 // (kAudio: the audio track's)
{
    static const juce::Colour c[] = { juce::Colour (0xffe38b5e), juce::Colour (0xff62b2c6), juce::Colour (0xffa093de),
                                      juce::Colour (0xffbcc76d), juce::Colour (0xffd98ab0), juce::Colour (0xff6fc4a2),
                                      juce::Colour (0xffe0b85a), juce::Colour (0xff8aa0d8) };
    return t == kTracks ? juce::Colour (0xffcfc3a4) : c[t & 7];
}
constexpr int kAudio = kTracks;     // the audio track (samples played by the app, not by SLOOP): the session's last column

// An audio clip: a sample (a file of the app's samples folder, at the engine's rate), played from `start` to `end`
// (the file is untouched), looped. Its own tempo says how many beats that is (rounded: at least one); it plays
// that many beats of the song, at the song's tempo: stretched (its pitch kept) or sped up like a record
struct AudioClip
{
    juce::String name, file;
    int64_t start = 0, end = 0;     // frames of the file
    double bpm = 120.0;             // its tempo, by ear (tap) or from its length in beats
    bool stretch = true;
    float gain = 1.0f;
    static constexpr double rate = 44100.0;   // (the samples are kept at the engine's rate)
    double seconds() const { return (double) juce::jmax ((int64_t) 1, end - start) / rate; }
    double lengthBeats() const { return seconds() * bpm / 60.0; }        // (what the start and end hold)
    int beats() const { return juce::jmax (1, juce::roundToInt (lengthBeats())); }   // what it plays
};

// A step's condition beyond its probability (Elektron / OP-Z style); the app resolves them
enum class Cond : uint8_t { always, oneOfTwo, twoOfTwo, oneOfFour, fill, notFill };

struct StepData
{
    uint8_t on = 0;
    uint8_t vel = 100;              // MIDI velocity
    uint8_t ratchet = 1;            // 1..4
    uint8_t prob = 100;             // %
    int8_t nudge = 0;               // -3..3 sixths of a step
    Cond cond = Cond::always;
    uint32_t rows = 0;              // synth steps: the piano roll rows played (bit r = row r)
};

struct LockData { uint8_t step = 0, param = 0; int16_t value = 0; };   // on `step`, SLOOP parameter `param` = value

struct ClipData
{
    uint8_t drum = 1;
    uint8_t length = kBar;                                          // 1..kMaxSteps
    std::array<std::array<StepData, kMaxSteps>, kLanes> lanes {};   // the drum track: [lane][step]
    std::array<StepData, kMaxSteps> notes {};                       // a synth track
    std::array<LockData, kMaxLocks> locks {};                       // parameter locks, any sound parameter
    uint16_t nLocks = 0;
    std::array<std::array<uint8_t, kRows>, kMaxSteps> noteLen {};   // a synth note's length in steps ([start][row]; 0, 1: one)

    // a synth note held over several steps: it starts on a step (its row's bit) and lasts noteLength steps
    int noteLength (int step, int row) const { return juce::jmax (1, (int) noteLen[(size_t) step][(size_t) row]); }
    int noteStart (int row, int step) const          // the first step of the note of `row` sounding on `step`, else -1
    {
        for (int s = step; s >= 0; --s)
            if (notes[(size_t) s].rows >> row & 1u) return s + noteLength (s, row) > step ? s : -1;
        return -1;
    }
    int maxLength (int row, int start) const         // up to the row's next note, or the clip's end
    {
        int s = start + 1;
        while (s < length && ! (notes[(size_t) s].rows >> row & 1u)) ++s;
        return s - start;
    }

    int16_t lock (int step, int param) const
    {
        for (int i = 0; i < nLocks; ++i)
            if (locks[(size_t) i].step == step && locks[(size_t) i].param == param) return locks[(size_t) i].value;
        return kNoLock;
    }
    bool setLock (int step, int param, int value)               // false: the track's locks are all used
    {
        for (int i = 0; i < nLocks; ++i)
            if (locks[(size_t) i].step == step && locks[(size_t) i].param == param) { locks[(size_t) i].value = (int16_t) value; return true; }
        if (nLocks == kMaxLocks) return false;
        locks[nLocks++] = { (uint8_t) step, (uint8_t) param, (int16_t) value };
        return true;
    }
    void clearLock (int step, int param)                         // param < 0: every lock of the step
    {
        int n = 0;
        for (int i = 0; i < nLocks; ++i)
            if (! (locks[(size_t) i].step == step && (param < 0 || locks[(size_t) i].param == param))) locks[(size_t) n++] = locks[(size_t) i];
        nLocks = (uint16_t) n;
    }

    void setStep (int row, int step, bool on)                   // a drum hit, or a one-step note of a row
    {
        if (drum) { lanes[(size_t) row][(size_t) step].on = on; return; }
        auto& x = notes[(size_t) step];
        x.rows = on ? x.rows | 1u << row : x.rows & ~(1u << row);
        x.on = x.rows != 0;
        noteLen[(size_t) step][(size_t) row] = on ? 1 : 0;
    }
    bool canDouble() const { return length * 2 <= kMaxSteps; }
    void doubleLength()                                          // played twice in a row, as one clip twice as long
    {
        const int n = length;
        for (int i = 0; i < n; ++i)
        {
            for (auto& ln : lanes) ln[(size_t) (n + i)] = ln[(size_t) i];
            notes[(size_t) (n + i)] = notes[(size_t) i];
            noteLen[(size_t) (n + i)] = noteLen[(size_t) i];
        }
        const int count = nLocks;
        for (int i = 0; i < count; ++i)
            setLock (locks[(size_t) i].step + n, locks[(size_t) i].param, locks[(size_t) i].value);   // (while there is room)
        length = (uint8_t) (n * 2);
    }
    void quantize()                                              // every step back on the grid
    {
        for (auto& ln : lanes) for (auto& x : ln) x.nudge = 0;
        for (auto& x : notes) x.nudge = 0;
    }

    bool stepOn (int row, int step) const
    {
        return drum ? lanes[(size_t) row][(size_t) step].on != 0 : (notes[(size_t) step].rows >> row & 1u) != 0;
    }
    bool hasLocks (int step) const
    {
        for (int i = 0; i < nLocks; ++i) if (locks[(size_t) i].step == step) return true;
        return false;
    }
    bool columnActive (int step) const
    {
        if (! drum) return notes[(size_t) step].rows != 0;
        for (auto& l : lanes) if (l[(size_t) step].on) return true;
        return false;
    }
};

struct Clip
{
    juce::String name;
    ClipData data;
};

// The knobs show four SLOOP parameters at a time (core.h P_*), page by page: the mix, the engine's four main
// ones (its macros, as the FM-1's HOME knobs), its two EDIT pages, the envelope, the effect sends
enum class Page { mix, macro, edit1, edit2, edit3, edit4, env, mod, fx };
struct KnobDef { juce::String label; int param = -1, min = 0, max = 127, def = 0; bool valid = false; };
constexpr int kParamMute = 40;      // core.h P_MUTE

// The sounds at start: a kit for DR, an engine and a preset for T1..T3 (sloop_engine.h se_*_name)
struct Sound { int engine, preset; };
constexpr int kDefaultKit = 15;                                         // HOUSE
constexpr std::array<Sound, kTracks> kDefaultSounds {{ { -1, kDefaultKit }, { 0, 2 }, { 1, 0 }, { 6, 6 },
                                                        { 13, 7 }, { 10, 9 }, { 3, 2 }, { 8, 0 } }};
    // -, ANALOG SUB BASS, DIGITAL RHODES, TRIO SYNC LEAD, OBXD SOFT PAD, PHYS NYLON GTR, LOFI 8BIT ARP, GRAIN LOFI CLOUD

// The key: a root and a scale. The piano roll and the keyboard only show its notes (row r = its r-th degree from
// the root), so a melody stays in key, and changing the key moves every clip into the new one (a row keeps its
// degree). C minor at first, as the mockup
struct ScaleDef { const char* name; std::array<int8_t, 12> steps; int size; };
inline const std::array<ScaleDef, 11>& scales()
{
    static const std::array<ScaleDef, 11> s {{
        { "Mineur", { 0, 2, 3, 5, 7, 8, 10 }, 7 },          { "Majeur", { 0, 2, 4, 5, 7, 9, 11 }, 7 },
        { "Dorien", { 0, 2, 3, 5, 7, 9, 10 }, 7 },          { "Phrygien", { 0, 1, 3, 5, 7, 8, 10 }, 7 },
        { "Lydien", { 0, 2, 4, 6, 7, 9, 11 }, 7 },          { "Mixolydien", { 0, 2, 4, 5, 7, 9, 10 }, 7 },
        { "Mineur harm.", { 0, 2, 3, 5, 7, 8, 11 }, 7 },    { "Penta maj.", { 0, 2, 4, 7, 9 }, 5 },
        { "Penta min.", { 0, 3, 5, 7, 10 }, 5 },            { "Blues", { 0, 3, 5, 6, 7, 10 }, 6 },
        { "Chromatique", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
    }};
    return s;
}
struct Key
{
    int root = 0;                   // 0 C .. 11 B
    int scale = 0;                  // scales()
    const ScaleDef& def() const { return scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scale)]; }
    bool isRoot (int row) const { return row % def().size == 0; }
    bool operator== (const Key& o) const { return root == o.root && scale == o.scale; }
};
inline int rowNote (int track, int row, const Key& key)
{
    static const int base[kTracks] = { 0, 36, 60, 72, 60, 48, 72, 60 };
    const auto& d = key.def();
    return juce::jlimit (0, 127, base[track] + key.root + 12 * (row / d.size) + d.steps[(size_t) (row % d.size)]);
}
inline juce::String pitchName (int pc)
{
    static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return n[((pc % 12) + 12) % 12];
}
inline juce::String noteName (int midi) { return pitchName (midi) + juce::String (midi / 12 - 1); }

struct Session
{
    std::array<std::vector<std::optional<Clip>>, kTracks> cols;
    std::vector<std::optional<AudioClip>> audio;     // the audio track's clips, a row each

    int scenes() const { return (int) cols[0].size(); }
    std::optional<Clip>& at (int t, int s) { return cols[(size_t) t][(size_t) s]; }
    const std::optional<Clip>& at (int t, int s) const { return cols[(size_t) t][(size_t) s]; }
    void addScene() { for (auto& c : cols) c.emplace_back(); audio.emplace_back(); }
    static Clip newClip (int t, juce::String name)
    {
        Clip c { std::move (name), {} };
        c.data.drum = isDrum (t) ? 1 : 0;
        c.data.length = kBar;
        return c;
    }
};

inline Session demoSession()
{
    Session s;
    for (int i = 0; i < 6; ++i) s.addScene();
    auto hit = [] (ClipData& c, int lane, std::initializer_list<int> steps, int vel)
    {
        for (int i : steps) { auto& x = c.lanes[(size_t) lane][(size_t) i]; x.on = 1; x.vel = (uint8_t) vel; }
    };
    // SLOOP's lanes: 0 kick, 2 snare, 3 clap, 4 hat, 5 open hat, 7 rim, 11 crash, 12 ride, 13 shaker
    auto groove = Session::newClip (0, "Groove");
    hit (groove.data, 0, { 0, 4, 8, 12 }, 120); hit (groove.data, 3, { 4, 12 }, 110);
    hit (groove.data, 4, { 2, 6, 10, 14 }, 80); hit (groove.data, 5, { 7, 15 }, 60); hit (groove.data, 7, { 3, 11 }, 60);
    hit (groove.data, 2, { 13, 14, 15 }, 70);
    for (int i : { 13, 14, 15 }) groove.data.lanes[2][(size_t) i].cond = Cond::fill;
    hit (groove.data, 11, { 0 }, 100); groove.data.lanes[11][0].cond = Cond::oneOfFour;
    groove.data.lanes[4][10].prob = 50;
    auto shaker = groove; shaker.name = "Groove + shaker"; hit (shaker.data, 13, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, 40);
    auto nokick = groove; nokick.name = "Sans kick"; for (auto& x : nokick.data.lanes[0]) x = {};
    auto half = Session::newClip (0, "Half-time"); hit (half.data, 0, { 0, 10 }, 120); hit (half.data, 3, { 8 }, 110);
    hit (half.data, 12, { 0, 2, 4, 6, 8, 10, 12, 14 }, 50);
    s.at (0, 0) = groove; s.at (0, 1) = shaker; s.at (0, 2) = nokick; s.at (0, 3) = half;

    auto notes = [] (ClipData& c, int step, std::initializer_list<int> rows, int vel)
    {
        auto& x = c.notes[(size_t) step];
        x.on = 1; x.vel = (uint8_t) vel;
        for (int r : rows) x.rows |= 1u << r;
    };
    auto bass = Session::newClip (1, "Ligne");
    notes (bass.data, 0, { 0 }, 110); notes (bass.data, 3, { 0 }, 80); notes (bass.data, 6, { 2 }, 100);
    notes (bass.data, 8, { 0 }, 110); notes (bass.data, 10, { 4 }, 100); notes (bass.data, 14, { 3 }, 90);
    auto bass2 = bass; bass2.name = "Ligne +"; notes (bass2.data, 12, { 0 }, 100); notes (bass2.data, 7, { 7 }, 90);
    bass2.data.notes[7].cond = Cond::oneOfTwo;
    s.at (1, 0) = bass; s.at (1, 1) = bass2;
    auto keys = Session::newClip (2, "Accords");
    notes (keys.data, 0, { 7, 9, 11 }, 80); notes (keys.data, 8, { 5, 7, 9 }, 80);
    for (int r : { 7, 9, 11 }) keys.data.noteLen[0][(size_t) r] = 6;    // held chords
    for (int r : { 5, 7, 9 }) keys.data.noteLen[8][(size_t) r] = 6;
    s.at (2, 0) = keys; s.at (2, 1) = keys; s.at (2, 2) = keys;
    auto lead = Session::newClip (3, "Motif");
    notes (lead.data, 2, { 4 }, 100); notes (lead.data, 5, { 6 }, 90); notes (lead.data, 10, { 7 }, 100);
    notes (lead.data, 13, { 6 }, 80); lead.data.notes[13].prob = 50;
    s.at (3, 0) = lead; s.at (3, 1) = lead;
    return s;
}
} // namespace sloop
