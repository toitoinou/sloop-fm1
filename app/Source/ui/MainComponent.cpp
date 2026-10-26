// SPDX-License-Identifier: GPL-3.0-only
#include "ui/MainComponent.h"
#include "ui/AudioEditor.h"
#include "ui/ProjectSheet.h"

namespace sloop
{
MainComponent::MainComponent()
{
    setLookAndFeel (&look);
    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &recButton, &tapButton, &stopClips, &fillButton, &bpm,
                                                             &rail, &tabSession, &tabGrid, &tabPads, &tabMix, &mixer, &projectButton, &clickButton, &undoButton,
                                                             &redoButton, &session,
                                                             &grid, &pads, &knobs })
        addAndMakeVisible (c);
    playButton.onClick = [this] { ctl.togglePlay(); };
    tapButton.onClick = [this] { tap(); };
    recButton.onClick = [this] { ctl.toggleRecord(); };
    recButton.setColour (juce::TextButton::buttonOnColourId, theme::rec);
    clickButton.onClick = [this] { auto p = ctl.prefs; p.metronome = ! p.metronome; ctl.setPrefs (p); };
    undoButton.onClick = [this] { ctl.undo(); };
    redoButton.onClick = [this] { ctl.redo(); };
    pads.onKey = [this] { openSettings(); };
    mixer.onRecorded = [this] (const juce::File& f) { shareRecording (f); };
    stopClips.onClick = [this] { ctl.stopAll(); };
    fillButton.onHold = [this] (bool on) { audio.setFill (on); };
    tabSession.onClick = [this] { showTab (Tab::session); };
    tabGrid.onClick = [this] { showTab (Tab::grid); };
    tabPads.onClick = [this] { showTab (Tab::pads); };
    tabMix.onClick = [this] { showTab (Tab::mix); };
    knobs.onSound = [this] { openSound(); };
    session.sessionGrid().onAudioEdit = [this] (int r) { openAudioEditor (r); };
    session.sessionGrid().onAudioImport = [this] (int r) { importAudio (r); };
    projectButton.onClick = [this] { openProject(); };
    ctl.changed = [this] { refresh(); };
    showTab (Tab::session);

    setupAudio();
    devices.addAudioCallback (&audio);
    setSize (1280, 800);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    devices.removeAudioCallback (&audio);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

void MainComponent::resized()
{
    // a phone (the short side under 500 dp): bigger text, thinner margins, every dp to the stage
    const bool phone = juce::jmin (getWidth(), getHeight()) < 500;
    theme::textScale = phone ? 1.2f : 1.0f;
    auto r = getLocalBounds();
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()); d != nullptr && isOnDesktop())
        r = d->safeAreaInsets.subtractedFrom (r);   // (the notch, the rounded corners)
    r = r.reduced (phone ? 6 : 12, phone ? 6 : 10);
    const int g = phone ? 6 : 8;
    juce::Rectangle<int> stage;
    bool undoInRail = false;                          // on a phone: undo / redo by the transport (room for the tabs)
    if (getWidth() > getHeight())
    {
        // landscape: rail | stage | device
        const bool big = getWidth() >= 1000;
        auto left = r.removeFromLeft (big ? 150 : phone ? 128 : 120);
        r.removeFromLeft (g);
        auto right = r.removeFromRight (big ? 300 : phone ? 216 : 200);
        r.removeFromRight (g);
        const int bh = big ? 46 : phone ? 44 : 40;
        auto top = left.removeFromTop (bh);
        playButton.setBounds (top.removeFromLeft (top.getWidth() / 2 - 3)); top.removeFromLeft (6);
        recButton.setBounds (top);
        left.removeFromTop (6);
        auto second = left.removeFromTop (bh);
        bpm.setBounds (second.removeFromLeft (second.getWidth() / 2 - 3)); second.removeFromLeft (6);
        tapButton.setBounds (second);
        if (phone)
        {
            left.removeFromTop (6);
            auto third = left.removeFromTop (bh - 4);
            undoButton.setBounds (third.removeFromLeft (third.getWidth() / 2 - 3)); third.removeFromLeft (6);
            redoButton.setBounds (third);
            undoInRail = true;
        }
        left.removeFromTop (g);
        rail.setBounds (left);
        auto perf = right.removeFromTop (phone ? bh : bh * 2 + 6);
        fillButton.setBounds (perf.removeFromLeft (perf.getWidth() / 2 - 3)); perf.removeFromLeft (6);
        stopClips.setBounds (perf);
        right.removeFromTop (g);
        knobs.setBounds (right.removeFromTop (juce::jmin (right.getHeight(), big ? 360 : right.getHeight())));
        stage = r;
    }
    else
    {
        // portrait: transport, performance, tracks, tabs, stage, knobs
        auto top = r.removeFromTop (44);
        playButton.setBounds (top.removeFromLeft (52)); top.removeFromLeft (6);
        recButton.setBounds (top.removeFromLeft (52)); top.removeFromLeft (6);
        undoButton.setBounds (top.removeFromLeft (44)); top.removeFromLeft (4);
        redoButton.setBounds (top.removeFromLeft (44)); top.removeFromLeft (6);
        undoInRail = true;
        tapButton.setBounds (top.removeFromRight (64)); top.removeFromRight (6);
        bpm.setBounds (top);
        r.removeFromTop (g);
        auto perf = r.removeFromTop (40);
        fillButton.setBounds (perf.removeFromLeft (perf.getWidth() / 2 - 3)); perf.removeFromLeft (6);
        stopClips.setBounds (perf);
        r.removeFromTop (g);
        rail.setBounds (r.removeFromTop (50));
        r.removeFromTop (g);
        knobs.setBounds (r.removeFromBottom (160));
        r.removeFromBottom (g);
        stage = r;
    }
    // the tabs, then undo, redo, the metronome on the right
    auto tabs = stage.removeFromTop (phone ? 42 : 38);
    const int iw = phone ? 44 : 48;
    clickButton.setBounds (tabs.removeFromRight (iw + 8)); tabs.removeFromRight (8);
    if (! undoInRail)
    {
        redoButton.setBounds (tabs.removeFromRight (iw)); tabs.removeFromRight (4);
        undoButton.setBounds (tabs.removeFromRight (iw)); tabs.removeFromRight (8);
    }
    const int tw = juce::jmin (100, (tabs.getWidth() - 24) / 5);
    for (auto* b : { &tabSession, &tabGrid, &tabPads, &tabMix, &projectButton })
    {
        b->setBounds (tabs.removeFromLeft (tw)); tabs.removeFromLeft (6);
    }
    stage.removeFromTop (g);
    for (auto* c : std::initializer_list<juce::Component*> { &session, &grid, &pads, &mixer }) c->setBounds (stage);
    if (sheet) sheet->setBounds (getLocalBounds());
}

void MainComponent::showTab (Tab t)
{
    tab = t;
    session.setVisible (t == Tab::session);
    grid.setVisible (t == Tab::grid);
    pads.setVisible (t == Tab::pads);
    tabSession.setToggleState (t == Tab::session, juce::dontSendNotification);
    tabGrid.setToggleState (t == Tab::grid, juce::dontSendNotification);
    tabPads.setToggleState (t == Tab::pads, juce::dontSendNotification);
    tabMix.setToggleState (t == Tab::mix, juce::dontSendNotification);
    mixer.setVisible (t == Tab::mix);
    grid.resized();
    refresh();
}

void MainComponent::refresh()
{
    playButton.setButtonText (ctl.playing ? "Stop" : "Play");
    playButton.setToggleState (ctl.playing, juce::dontSendNotification);
    recButton.setToggleState (ctl.recording, juce::dontSendNotification);
    clickButton.setToggleState (ctl.prefs.metronome, juce::dontSendNotification);
    midi.setClockOut (ctl.prefs.midiClockOut);
    undoButton.setEnabled (ctl.canUndo());
    redoButton.setEnabled (ctl.canRedo());
    tabGrid.setButtonText (isDrum (ctl.sel) ? "Grille" : "Notes");
    session.refresh();
    grid.resized();
    for (auto* c : std::initializer_list<juce::Component*> { &rail, &grid, &pads, &knobs, &bpm }) c->repaint();
    if (sheet) sheet->repaint();
}

void MainComponent::timerCallback()
{
    ctl.poll();
    if (++ticks % (30 * 30) == 0) autosave();           // every 30 s, besides when the app goes to the background
}

void MainComponent::autosave()
{
    auto json = ctl.snapshot().toJson();
    if (json == lastAutosave) return;
    if (store::write (store::autosaveFile(), ctl.snapshot())) lastAutosave = json;
}

void MainComponent::showSheet (std::unique_ptr<juce::Component> s)
{
    sheet = std::move (s);
    addAndMakeVisible (*sheet);
    sheet->setBounds (getLocalBounds());
}
void MainComponent::openSheet (int row, int step)
{
    showSheet (std::make_unique<StepSheet> (ctl, row, step, [this]
    {
        juce::MessageManager::callAsync ([this] { sheet.reset(); refresh(); });
    }));
}
void MainComponent::openProject()
{
    showSheet (std::make_unique<ProjectSheet> (ctl, [this]
    {
        juce::MessageManager::callAsync ([this] { sheet.reset(); refresh(); });
    }, [this] { juce::MessageManager::callAsync ([this] { openSettings(); }); }));
}
void MainComponent::openSettings()
{
    showSheet (std::make_unique<SettingsSheet> (ctl, [this]
    {
        juce::MessageManager::callAsync ([this] { sheet.reset(); refresh(); });
    }, [this] { return audioText(); }, [this] { nextBufferSize(); },
       [this] { const auto names = midi.inputNames(); return names.isEmpty() ? juce::String::fromUTF8 ("Aucune entrée MIDI") : names.joinIntoString (", "); }));
}
void MainComponent::openAudioEditor (int row)
{
    showSheet (std::make_unique<AudioEditor> (ctl, row, [this]
    {
        juce::MessageManager::callAsync ([this] { sheet.reset(); refresh(); });
    }));
}
void MainComponent::importAudio (int row)
{
    juce::PopupMenu m, recent;
    const auto recordings = SampleBank::recordings();
    for (int i = 0; i < juce::jmin (20, recordings.size()); ++i) recent.addItem (100 + i, recordings[i].getFileNameWithoutExtension());
    m.addSectionHeader (juce::String::fromUTF8 ("Un son sur la piste audio"));
    m.addItem (1, juce::String::fromUTF8 ("Un fichier… (WAV, AIFF, FLAC, Ogg, MP3)"));
    m.addSubMenu (juce::String::fromUTF8 ("Un enregistrement de l'app"), recent, ! recordings.isEmpty());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&session),
                     [this, row, recordings] (int r)
                     {
                         if (r >= 100) { importAudioFrom (row, juce::URL (recordings[r - 100])); return; }
                         if (r != 1) return;
                         importing = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Importer un son"),
                             juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                         importing->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                 [this, row] (const juce::FileChooser& fc)
                                                 {
                                                     if (auto url = fc.getURLResult(); ! url.isEmpty()) importAudioFrom (row, url);
                                                 });
                     });
}
void MainComponent::importAudioFrom (int row, const juce::URL& url)
{
    juce::String error;
    if (ctl.importAudio (row, url, error)) { openAudioEditor (row); return; }
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, juce::String::fromUTF8 ("Import"), error);
}
void MainComponent::snapshotAudioClip()
{
    // two bars at 96 BPM: a thump on each beat, a hiss between, a little silence after (the end to trim)
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Loop 96bpm.wav");
    const int beat = (int) (AudioClip::rate * 60.0 / 96.0);
    juce::AudioBuffer<float> b (2, beat * 8 + beat / 2);
    b.clear();
    juce::Random rnd (7);
    for (int k = 0; k < 8; ++k)
        for (int i = 0; i < beat; ++i)
        {
            const float t = (float) i / (float) AudioClip::rate;
            float v = 0.9f * std::sin (6.2831853f * 55.0f * t) * std::exp (-t * 18.0f);
            if (i > beat / 2) v += 0.25f * (rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) (i - beat / 2) / 2000.0f);
            b.setSample (0, k * beat + i, v);
            b.setSample (1, k * beat + i, v);
        }
    juce::WavAudioFormat wav;
    file.deleteFile();
    if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (file.createOutputStream().release(), AudioClip::rate, 2, 24, {}, 0)); w != nullptr)
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    juce::String error;
    ctl.importAudio (0, juce::URL (file), error);
}
void MainComponent::openSound()
{
    showSheet (std::make_unique<SoundSheet> (ctl, [this]
    {
        juce::MessageManager::callAsync ([this] { sheet.reset(); refresh(); });
    }));
}

// the low-latency path: Oboe (AAudio, or OpenSL ES on old phones) and a small buffer; the settings show the buffer
// and a tap there tries the next size, to find the smallest one that does not crackle on this phone
void MainComponent::setupAudio()
{
    devices.initialiseWithDefaultDevices (0, 2);     // no input: no microphone permission on Android
    for (auto* type : devices.getAvailableDeviceTypes())
        if (type->getTypeName() == "Oboe")
        {
            devices.setCurrentAudioDeviceType ("Oboe", true);
            break;
        }
    if (auto* dev = devices.getCurrentAudioDevice())
    {
        auto setup = devices.getAudioDeviceSetup();
        int best = dev->getDefaultBufferSize();
        for (int size : dev->getAvailableBufferSizes())
            if (size >= 128) { best = juce::jmin (best, size); break; }
        setup.bufferSize = best;
        devices.setAudioDeviceSetup (setup, true);
    }
}

void MainComponent::nextBufferSize()
{
    auto* dev = devices.getCurrentAudioDevice();
    if (dev == nullptr) return;
    auto sizes = dev->getAvailableBufferSizes();
    if (sizes.isEmpty()) return;
    const int now = dev->getCurrentBufferSizeSamples();
    int next = sizes[0];
    for (int size : sizes)
        if (size > now && size <= 2048) { next = size; break; }
    auto setup = devices.getAudioDeviceSetup();
    setup.bufferSize = next;
    devices.setAudioDeviceSetup (setup, true);
}

// the audio device and its buffer (the settings: a tap tries the next size, to find the smallest one that does
// not crackle on this phone)
juce::String MainComponent::audioText()
{
    auto* dev = devices.getCurrentAudioDevice();
    if (dev == nullptr) return "Pas d'audio";
    const double rate = dev->getCurrentSampleRate();
    const int buffer = dev->getCurrentBufferSizeSamples(), extra = dev->getOutputLatencyInSamples();
    const double ms = rate > 0 ? (buffer + extra) * 1000.0 / rate : 0.0;
    return devices.getCurrentAudioDeviceType() + juce::String::fromUTF8 (" · buffer ") + juce::String (buffer)
           + juce::String::fromUTF8 (" · ≈ ") + juce::String (ms, 1) + " ms";
}

// a recording just made: the phone's share sheet (Drive, a message, Files...); a computer: where to save a copy
void MainComponent::shareRecording (const juce::File& file)
{
    if (! file.existsAsFile()) return;
   #if JUCE_ANDROID || JUCE_IOS
    sharing = juce::ContentSharer::shareFilesScoped ({ juce::URL (file) }, [] (bool, const juce::String&) {}, this);
   #else
    saving = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Enregistrer l'audio"),
                                                  juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (file.getFileName()),
                                                  "*.wav");
    saving->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [file] (const juce::FileChooser& fc)
                         {
                             if (auto out = fc.getResult(); out != juce::File()) file.copyFileTo (out);
                         });
   #endif
}

void MainComponent::tap()
{
    const double t = juce::Time::getMillisecondCounterHiRes();
    taps.erase (std::remove_if (taps.begin(), taps.end(), [t] (double x) { return t - x > 2000.0; }), taps.end());
    taps.push_back (t);
    if (taps.size() > 1)
        ctl.setBpm (juce::roundToInt (60000.0 / ((taps.back() - taps.front()) / (double) (taps.size() - 1))));
}
} // namespace sloop
