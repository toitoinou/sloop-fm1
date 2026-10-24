// SPDX-License-Identifier: GPL-3.0-only
// The audio side: SLOOP's engine (mobile/engine) driven from the audio callback. The UI never touches the engine:
// it posts commands (a lock-free queue), the audio thread applies them between two engine blocks and publishes
// what plays in atomics the UI reads on a timer.
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
    void play (bool on);
    void setBpm (int bpm);
    void setFill (bool on);
    void setParam (int track, int param, int value);
    void setSound (int track, int engine, int preset);
    void setKit (int kit);
    void drumHit (int lane, int velocity);
    void note (int track, int midi, bool on);
    // what a track plays: a chain of clips (one = a single clip, none = the track stops), from `when`
    void setPlaylist (int track, When when, const std::vector<std::pair<int, const ClipData*>>& clips);

    // ---- read by the UI
    std::atomic<int> playingSlot[kTracks];   // the session row each track plays, -1 none
    std::atomic<int> position[kTracks];      // its step, -1 stopped
    std::atomic<bool> running { false };
    std::atomic<int> hits[kTracks];          // counts up when a track plays a step (the rail's LEDs)
    std::atomic<int> knobValue[kTracks][kKnobs];   // the knobs' parameters as the engine has them (a sound sets
    std::atomic<int> knobSerial { 0 };             // its sends): counts up when they change

    // ---- juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void prepare (double sampleRate, int blockSize);   // (also for the self-test, without a device)
    void audioDeviceStopped() override {}

private:
    enum class Type : uint8_t { play, bpm, fill, param, sound, kit, hit, note, playlist };
    struct Command
    {
        Type type;
        When when = When::now;
        int a = 0, b = 0, c = 0;
        int n = 0;
        int slots[kChainMax] {};
        ClipData clips[kChainMax];
    };
    void post (Command&& c);
    void apply (Command& c);

    struct TrackState
    {
        int n = 0, idx = 0;                  // the playlist and where it is
        int slots[kChainMax] {};
        ClipData clips[kChainMax];
        bool pending = false;                // a playlist waiting for the next bar
        Command next;
        int loops = 0;                       // times the clip has looped (1:2, 1:4 conditions)
        uint32_t seed = 1;                   // this loop's dice (probabilities)
        int lastPos = -1;
        bool lastStepPending = false;        // the next loop is written except the step playing now
        int reportSlot = -1;
    };
    void renderEngineBlock();
    void publishKnobs (int track);
    void startTransport();
    void prepareNextLoop (int t);
    void writeSteps (int t, int onlyStep, int skipStep);
    bool stepPlays (const TrackState& s, const StepData& x, int lane, int step) const;

    juce::AbstractFifo fifo { 16 };
    std::vector<Command> queue;
    std::array<TrackState, kTracks> tracks;
    bool fill = false;
    uint32_t rng = 0x12345678u;

    // engine (44.1 kHz) -> device rate
    double deviceRate = 44100.0;
    juce::LagrangeInterpolator interp[2];
    juce::AudioBuffer<float> staging { 2, 4096 };
    int staged = 0;
};
} // namespace sloop
