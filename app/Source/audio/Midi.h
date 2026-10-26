// SPDX-License-Identifier: GPL-3.0-only
// MIDI: every input there is (USB, Bluetooth once paired, the computer's), opened as it appears. Its notes play the
// track selected at once (the audio thread takes them: AudioEngine::midiIn) and go into a clip when REC is on; CC
// 21..24 and 70..73 turn the four knobs; START / STOP start and stop the song. Out: the MIDI clock (24 a beat) and
// START / STOP to every output, when the settings say so.
#pragma once
#include "control/Controller.h"

namespace sloop
{
class MidiHub : private juce::MidiInputCallback, private juce::HighResolutionTimer
{
public:
    explicit MidiHub (Controller&);
    ~MidiHub() override;
    juce::StringArray inputNames() const;
    void setClockOut (bool on) { clockOut = on; }
    static void pairBluetooth();                      // Android / iOS: the system's pairing of a Bluetooth MIDI device

private:
    void open();
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;   // (a MIDI thread)
    void hiResTimerCallback() override;               // the clock out, every millisecond
    Controller& ctl;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    std::vector<std::unique_ptr<juce::MidiOutput>> outputs;
    juce::CriticalSection outputLock;
    juce::MidiDeviceListConnection connection;
    std::atomic<bool> clockOut { false };
    bool wasRunning = false;
    double started = 0;
    int64_t ticks = 0;
};
} // namespace sloop
