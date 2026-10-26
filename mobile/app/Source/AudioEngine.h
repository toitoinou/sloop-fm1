// SPDX-License-Identifier: GPL-3.0-only
// The audio side: SLOOP's engine (mobile/engine) driven from the audio callback. The UI never touches the engine:
// it posts commands (a lock-free queue), the audio thread applies them between two engine blocks and publishes
// what plays in atomics the UI reads on a timer.
//
// The engine plays each track over its 64 steps in a loop; the app writes them just ahead of the playhead (a few
// steps), from whatever clip is due there. So a clip can have any length (1..64), a chain can mix lengths, a clip
// launched while playing starts on the next bar, and an edit is heard at once.
#pragma once
#include "Model.h"
#include <atomic>

namespace sloop
{
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    enum class When : uint8_t { now, nextBar, update };

    AudioEngine();

    // ---- from the message thread
    void play (bool on, bool countIn = false);          // countIn: one bar of clicks first
    void setBpm (int bpm);
    void setSwing (int amount);                         // 0..100 (se_set_swing)
    void setMetronome (bool on);
    void setKey (const Key&);
    void setFill (bool on);
    void setParam (int track, int param, int value);
    void setSound (int track, int engine, int preset);
    void setParams (int track, const std::array<int, kParamSlots>& values);   // every parameter at once (a project)
    void setKit (int kit);
    void drumHit (int lane, int velocity);
    void note (int track, int midi, bool on);
    // note repeat: a drum lane (drum track) or a note hit again and again at a rate (repeatSteps), on the tempo's grid
    // while the song plays; stop: the same track and id
    void repeat (int track, int id, int velocity, int rate, bool on);
    // what a track plays: a chain of clips (one = a single clip, none = the track stops), from `when`; update:
    // new data for the clips of these rows, wherever they are (playing, or waiting for the bar)
    void setPlaylist (int track, When when, const std::vector<std::pair<int, const ClipData*>>& clips);
    void renumber (int from, int delta);               // the session's rows from `from` on moved by delta
    void setSolo (unsigned mask);                       // bit t: track t soloed (0: none)
    void setGlobal (int id, int value);                 // a global setting (sloop_engine.h SE_G_*)
    std::atomic<float> masterGain { 1.0f };             // the master volume, at the output

    // ---- read by the UI
    std::atomic<int> playingSlot[kTracks];   // the session row each track plays, -1 none
    std::atomic<int> position[kTracks];      // its step in its clip, -1 stopped
    std::atomic<bool> running { false };
    std::atomic<int> countIn { 0 };          // the count-in's beats still to click (0: none)
    std::atomic<int> hits[kTracks];          // counts up when a track plays a step (the rail's LEDs)
    std::atomic<int> paramValue[kTracks][kParamSlots];   // every SLOOP parameter as the engine has it (a new sound
    std::atomic<int> paramSerial { 0 };                  // sets them all): counts up when they change
    std::atomic<float> stepPhase[kTracks];               // how far into its step each track is, 0..1 (recording)
    std::atomic<int> peak[kTracks];                      // each track's loudest since the UI took it (exchange 0)
    std::atomic<float> masterPeak { 0.0f };              // the output's loudest, 0..1
    std::atomic<int> barStep[kTracks];                   // its step in the bar, clip or not (-1 stopped): a new clip
                                                         // recorded on an empty track starts where the bar is

    // ---- juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void prepare (double sampleRate, int blockSize);   // (also for the self-test, without a device)
    void audioDeviceStopped() override {}

private:
    enum class Type : uint8_t { play, bpm, swing, metronome, key, fill, param, params, sound, kit, hit, note, playlist, renumber, repeat, solo, global };
    struct Command
    {
        Type type;
        When when = When::now;
        int a = 0, b = 0, c = 0;
        int n = 0;
        int slots[kChainMax] {};
        ClipData clips[kChainMax];
        std::array<int, kParamSlots> values {};
    };
    void post (Command&& c);
    void apply (Command& c);

    struct TrackState
    {
        int n = 0, idx = 0;                  // the playlist and the clip playing in it
        int slots[kChainMax] {};
        ClipData clips[kChainMax];
        bool pending = false;                // a playlist waiting for the bar at pendingAt
        int64_t pendingAt = 0;
        Command next;
        int64_t start = 0;                   // the step (since PLAY) where the clip playing began this pass
        int passes = 0;                      // the passes of that clip in a row so far (1:2, 1:4 conditions)
        int64_t cur = -1;                    // the step playing, since PLAY (-1: none yet)
        int64_t written = -1;                // the engine has the steps up to there
        int lastPos = -1;                    // the engine's step (0..63) last seen
        uint32_t seed = 1;                   // the dice of the passes (probabilities): one roll a pass
    };
    struct Where { int idx = -1; int64_t start = 0; int passes = 0; bool fromNext = false; };
    Where locate (const TrackState&, int64_t step) const;      // the clip due at a step, and its pass
    void commit (int t, int64_t step);                          // the playhead is there: what plays now
    void writeStep (int t, int64_t step);
    void rewrite (int t);                                       // the steps ahead again (an edit, a launch)
    void renderEngineBlock();
    void publishParams (int track);
    void startTransport();
    void stopTransport();
    bool stepPlays (uint32_t seed, int passes, const StepData& x, int lane, int step) const;

    juce::AbstractFifo fifo { 32 };
    std::vector<Command> queue;
    std::array<TrackState, kTracks> tracks;
    bool fill = false;
    int tempo = 118;
    Key key;
    struct Repeat { int track = -1, id = 0, vel = 100; double interval = 0, next = 0, off = -1; };
    std::array<Repeat, 8> repeats {};
    double clock = 0;                                    // frames rendered (the repeats' time)
    void runRepeats (int frames);
    bool countingIn = false;
    double countFrames = 0;
    std::array<int, kTracks> framesInStep {};
    uint32_t rng = 0x12345678u;

    // engine (44.1 kHz) -> device rate
    double deviceRate = 44100.0;
    juce::LagrangeInterpolator interp[2];
    juce::AudioBuffer<float> staging { 2, 4096 };
    int staged = 0;
};
} // namespace sloop
