// SPDX-License-Identifier: GPL-3.0-only
// The audio clip editor (a whole-screen sheet): the sample's waveform, its start and its end (two handles: the file
// is untouched), its tempo by hand or by tapping along while it plays, how many beats that makes, stretched or like
// a record; listen to it alone, launch it with the song, give the song its tempo.
#pragma once
#include "control/Controller.h"

namespace sloop
{
class AudioEditor : public juce::Component, private juce::Timer
{
public:
    AudioEditor (Controller& c, int row, std::function<void()> close);
    ~AudioEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    AudioClip clip() const;
    void edit (const std::function<void (AudioClip&)>& change);   // the clip changed (one step back per gesture)
    void sync();
    void computePeaks();
    juce::Rectangle<int> waveRect() const;
    bool wide() const { return getHeight() < 600 && getWidth() > getHeight() * 3 / 2; }   // (a phone on its side)
    double xToFrame (float x) const;
    float frameToX (double frame) const;
    void tapTempo();
    void listen (bool on);

    Controller& ctl;
    const int row;
    std::function<void()> onClose;
    std::shared_ptr<const SampleData> sample;
    int64_t viewStart = 0, viewEnd = 1;                  // the frames in sight
    bool zoomed = false;                                 // the loop in sight, else the whole file
    std::vector<std::pair<float, float>> peaks;          // min / max per pixel column, for the view
    int peaksFor = -1;
    int dragging = 0;                                    // 1 start, 2 end
    bool listening = false;
    std::vector<double> taps;

    juce::TextButton ok { "OK" }, play, launch { juce::String::fromUTF8 ("Lancer avec le morceau") }, songTempo,
        zoom, tap { "TAP" }, slower { juce::String::fromUTF8 ("−1") }, slowerFine { juce::String::fromUTF8 ("−0.1") },
        faster { "+1" }, fasterFine { "+0.1" }, half { juce::String::fromUTF8 ("÷2") }, twice { juce::String::fromUTF8 ("×2") },
        snap { juce::String::fromUTF8 ("Fin sur les temps") }, startLeft { "<" }, startRight { ">" },
        endLeft { "<" }, endRight { ">" }, stretch { juce::String::fromUTF8 ("Stretch (garde la hauteur)") },
        varispeed { juce::String::fromUTF8 ("Vitesse (comme un disque)") };
};
} // namespace sloop
