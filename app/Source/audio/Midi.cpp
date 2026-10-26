// SPDX-License-Identifier: GPL-3.0-only
#include "audio/Midi.h"

namespace sloop
{
MidiHub::MidiHub (Controller& c) : ctl (c)
{
    open();
    connection = juce::MidiDeviceListConnection::make ([this] { open(); });
    startTimer (1);
}

MidiHub::~MidiHub()
{
    stopTimer();
    for (auto& in : inputs) in->stop();
}

// every device not open yet (a device unplugged: its entry stays closed, a new one is opened)
void MidiHub::open()
{
    for (const auto& d : juce::MidiInput::getAvailableDevices())
    {
        const bool known = std::any_of (inputs.begin(), inputs.end(), [&d] (auto& i) { return i->getIdentifier() == d.identifier; });
        if (known) continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            inputs.push_back (std::move (in));
        }
    }
    const juce::ScopedLock sl (outputLock);
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
    {
        const bool known = std::any_of (outputs.begin(), outputs.end(), [&d] (auto& o) { return o->getIdentifier() == d.identifier; });
        if (! known)
            if (auto out = juce::MidiOutput::openDevice (d.identifier)) outputs.push_back (std::move (out));
    }
}

juce::StringArray MidiHub::inputNames() const
{
    juce::StringArray names;
    const auto there = juce::MidiInput::getAvailableDevices();
    for (auto& in : inputs)
        if (std::any_of (there.begin(), there.end(), [&in] (auto& d) { return d.identifier == in->getIdentifier(); }))
            names.add (in->getName());
    return names;
}

void MidiHub::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isNoteOnOrOff())
    {
        ctl.audio.midiIn.addMessageToQueue (m);       // heard at once
        const int note = m.getNoteNumber(), vel = m.isNoteOn() ? m.getVelocity() : 0;
        juce::MessageManager::callAsync ([c = &ctl, note, vel] { c->midiNote (note, vel); });   // (recorded)
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        juce::MessageManager::callAsync ([c = &ctl, cc, v] { c->midiCC (cc, v); });
    }
    else if (m.isMidiStart() || m.isMidiContinue() || m.isMidiStop())
    {
        const bool play = ! m.isMidiStop();
        juce::MessageManager::callAsync ([c = &ctl, play] { if (c->playing != play) c->togglePlay(); });
    }
}

void MidiHub::hiResTimerCallback()
{
    const bool running = ctl.audio.running.load();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! clockOut.load())
    {
        wasRunning = false;
        return;
    }
    const juce::ScopedLock sl (outputLock);
    auto send = [this] (const juce::MidiMessage& msg) { for (auto& o : outputs) o->sendMessageNow (msg); };
    if (running != wasRunning)
    {
        wasRunning = running;
        send (running ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiStop());
        started = now;
        ticks = 0;
    }
    if (! running) return;
    const double tick = 60000.0 / (juce::jmax (20, ctl.bpm) * 24.0);   // ms
    while (now >= started + (double) ticks * tick)
    {
        send (juce::MidiMessage::midiClock());
        ++ticks;
    }
}

void MidiHub::pairBluetooth()
{
   #if JUCE_ANDROID || JUCE_IOS
    juce::RuntimePermissions::request (juce::RuntimePermissions::bluetoothMidi, [] (bool granted)
    {
        if (granted) juce::BluetoothMidiDevicePairingDialogue::open();
    });
   #endif
}
} // namespace sloop
