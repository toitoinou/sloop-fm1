// SPDX-License-Identifier: GPL-3.0-only
#include "AudioEditor.h"
#include "Theme.h"

namespace sloop
{
using theme::font;

AudioEditor::AudioEditor (Controller& c, int r, std::function<void()> close)
    : ctl (c), row (r), onClose (std::move (close))
{
    sample = ctl.samples.get (clip().file);
    viewEnd = sample != nullptr ? sample->buffer.getNumSamples() : 1;
    for (auto* b : { &ok, &play, &launch, &songTempo, &zoom, &tap, &slower, &slowerFine, &faster, &fasterFine, &half,
                     &twice, &snap, &startLeft, &startRight, &endLeft, &endRight, &stretch, &varispeed })
        addAndMakeVisible (b);
    ok.setToggleState (true, juce::dontSendNotification);
    ok.onClick = [this] { onClose(); };
    play.onClick = [this] { listen (! listening); };
    launch.onClick = [this] { listen (false); ctl.launchAudio (row); if (! ctl.playing) ctl.togglePlay(); };
    songTempo.onClick = [this] { ctl.setBpm (juce::roundToInt (clip().bpm)); sync(); };
    zoom.onClick = [this] { zoomed = ! zoomed; sync(); };
    tap.onClick = [this] { tapTempo(); };
    auto nudgeTempo = [this] (double d)
    {
        edit ([d] (AudioClip& a) { a.bpm = juce::jlimit ((double) kMinBpm, (double) kMaxBpm, std::round ((a.bpm + d) * 10.0) / 10.0); });
    };
    slower.onClick = [nudgeTempo] { nudgeTempo (-1.0); };
    slowerFine.onClick = [nudgeTempo] { nudgeTempo (-0.1); };
    faster.onClick = [nudgeTempo] { nudgeTempo (1.0); };
    fasterFine.onClick = [nudgeTempo] { nudgeTempo (0.1); };
    // half / twice the tempo: twice / half as many beats (a tap often finds half the tempo, or twice)
    half.onClick = [this] { edit ([] (AudioClip& a) { a.bpm = juce::jmax ((double) kMinBpm, a.bpm / 2.0); }); };
    twice.onClick = [this] { edit ([] (AudioClip& a) { a.bpm = juce::jmin ((double) kMaxBpm, a.bpm * 2.0); }); };
    // the end on a whole number of beats at the clip's tempo (the loop then lasts exactly its beats)
    snap.onClick = [this]
    {
        const int64_t n = sample != nullptr ? sample->buffer.getNumSamples() : 1;
        edit ([n] (AudioClip& a)
        {
            const double frames = a.beats() * 60.0 / a.bpm * AudioClip::rate;
            a.end = juce::jlimit (a.start + 1, n, a.start + (int64_t) std::llround (frames));
        });
    };
    // the start and the end by 10 ms (a drag on the waveform for more)
    auto move = [this] (bool end, int dir)
    {
        const int64_t n = sample != nullptr ? sample->buffer.getNumSamples() : 1, step = (int64_t) (AudioClip::rate / 100.0) * dir;
        edit ([=] (AudioClip& a)
        {
            if (end) a.end = juce::jlimit (a.start + 1, n, a.end + step);
            else a.start = juce::jlimit ((int64_t) 0, a.end - 1, a.start + step);
        });
    };
    startLeft.onClick = [move] { move (false, -1); };
    startRight.onClick = [move] { move (false, 1); };
    endLeft.onClick = [move] { move (true, -1); };
    endRight.onClick = [move] { move (true, 1); };
    stretch.onClick = [this] { edit ([] (AudioClip& a) { a.stretch = true; }); };
    varispeed.onClick = [this] { edit ([] (AudioClip& a) { a.stretch = false; }); };
    for (auto* b : { &slower, &slowerFine, &faster, &fasterFine, &startLeft, &startRight, &endLeft, &endRight, &half, &twice })
        b->setRepeatSpeed (400, 80);                     // (held: again and again)
    startTimerHz (30);
    sync();
}

AudioEditor::~AudioEditor() { listen (false); }

AudioClip AudioEditor::clip() const
{
    return row < ctl.session.scenes() && ctl.session.audio[(size_t) row] ? *ctl.session.audio[(size_t) row] : AudioClip {};
}

void AudioEditor::edit (const std::function<void (AudioClip&)>& change)
{
    auto c = clip();
    if (c.file.isEmpty()) return;
    const auto before = c;
    change (c);
    ctl.setAudioClip (row, c);
    if (listening && (c.start != before.start || c.end != before.end))   // (from its start again: as it is now)
        listen (true);
    sync();
}

void AudioEditor::listen (bool on)
{
    listening = on;
    auto c = ctl.playerClip (clip());
    c.stretch = false;                                   // (at its own pace: as it is)
    ctl.audio.preview (c, on && c.sample != nullptr);
    if (! on) taps.clear();
    sync();
}

void AudioEditor::tapTempo()
{
    // the taps of the last 2 s: their mean interval (the first tap starts listening if it was not)
    const double t = juce::Time::getMillisecondCounterHiRes();
    if (! listening) listen (true);
    taps.erase (std::remove_if (taps.begin(), taps.end(), [t] (double x) { return t - x > 2000.0; }), taps.end());
    taps.push_back (t);
    if (taps.size() < 3) return;
    const double bpm = 60000.0 / ((taps.back() - taps.front()) / (double) (taps.size() - 1));
    edit ([bpm] (AudioClip& a) { a.bpm = juce::jlimit ((double) kMinBpm, (double) kMaxBpm, std::round (bpm * 10.0) / 10.0); });
}

void AudioEditor::sync()
{
    const auto c = clip();
    play.setButtonText (listening ? juce::String::fromUTF8 ("■ Stop") : juce::String::fromUTF8 ("▶ Écouter"));
    play.setToggleState (listening, juce::dontSendNotification);
    songTempo.setButtonText ("Tempo du morceau : " + juce::String (juce::roundToInt (c.bpm)));
    zoom.setButtonText (zoomed ? juce::String ("Tout le fichier") : juce::String ("Zoom sur la boucle"));
    stretch.setToggleState (c.stretch, juce::dontSendNotification);
    varispeed.setToggleState (! c.stretch, juce::dontSendNotification);
    const int64_t n = sample != nullptr ? sample->buffer.getNumSamples() : 1;
    if (dragging != 0) {}                                // (the view stays put under a finger)
    else if (zoomed)
    {
        const int64_t margin = juce::jmax ((int64_t) 2000, (c.end - c.start) / 10);
        viewStart = juce::jmax ((int64_t) 0, c.start - margin);
        viewEnd = juce::jmin (n, c.end + margin);
    }
    else { viewStart = 0; viewEnd = n; }
    peaksFor = -1;
    repaint();
}

juce::Rectangle<int> AudioEditor::waveRect() const
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (44);
    return r.withHeight (wide() ? juce::jmax (70, r.getHeight() - 44 - 3 * (36 + 6) - 20 - 4) : juce::jmax (90, r.getHeight() * 40 / 100));
}
double AudioEditor::xToFrame (float x) const
{
    const auto w = waveRect().toFloat();
    return (double) viewStart + (double) (x - w.getX()) / w.getWidth() * (double) (viewEnd - viewStart);
}
float AudioEditor::frameToX (double frame) const
{
    const auto w = waveRect().toFloat();
    return w.getX() + (float) ((frame - (double) viewStart) / (double) juce::jmax ((int64_t) 1, viewEnd - viewStart)) * w.getWidth();
}

void AudioEditor::computePeaks()
{
    const int cols = juce::jmax (1, waveRect().getWidth());
    if (peaksFor == cols) return;
    peaksFor = cols;
    peaks.assign ((size_t) cols, { 0.0f, 0.0f });
    if (sample == nullptr) return;
    const auto* l = sample->buffer.getReadPointer (0);
    const auto* r = sample->buffer.getReadPointer (1);
    const double per = (double) (viewEnd - viewStart) / cols;
    for (int x = 0; x < cols; ++x)
    {
        const auto a = viewStart + (int64_t) (x * per), b = juce::jmax (a + 1, viewStart + (int64_t) ((x + 1) * per));
        const int64_t stride = juce::jmax ((int64_t) 1, (b - a) / 256);   // (a long file: a few hundred samples a column)
        float lo = 0, hi = 0;
        for (int64_t i = a; i < b && i < sample->buffer.getNumSamples(); i += stride)
        {
            const float v = 0.5f * (l[i] + r[i]);
            lo = juce::jmin (lo, v);
            hi = juce::jmax (hi, v);
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

void AudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    const auto c = clip();
    auto top = getLocalBounds().reduced (12).removeFromTop (36);
    g.setColour (theme::fg);
    g.setFont (font (19.0f, true));
    g.drawText (c.name, top.withTrimmedRight (100), juce::Justification::centredLeft, true);

    auto w = waveRect().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (w, theme::radius);
    computePeaks();
    const auto col = theme::track (kAudio);
    const float mid = w.getCentreY(), amp = w.getHeight() * 0.46f;
    const float xs = frameToX ((double) c.start), xe = frameToX ((double) c.end);
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const float px = w.getX() + (float) x;
        const bool in = px >= xs && px <= xe;
        g.setColour (in ? col : theme::dim.withAlpha (0.5f));
        g.fillRect (px, mid - peaks[(size_t) x].second * amp, 1.0f, juce::jmax (1.0f, (peaks[(size_t) x].second - peaks[(size_t) x].first) * amp));
    }
    // the beats from the start at the clip's tempo (a bar's first: brighter): the tempo by eye
    const double beat = 60.0 / c.bpm * AudioClip::rate;
    for (int k = 0; k <= c.beats() + 64; ++k)
    {
        const float x = frameToX ((double) c.start + k * beat);
        if (x > w.getRight()) break;
        g.setColour (theme::fg.withAlpha (k % 4 == 0 ? 0.45f : 0.15f));
        g.fillRect (x, w.getY(), 1.0f, w.getHeight());
    }
    // outside the loop: dimmed; the two handles
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRect (w.withRight (juce::jmax (w.getX(), xs)));
    g.fillRect (w.withLeft (juce::jmin (w.getRight(), xe)));
    for (float x : { xs, xe })
    {
        g.setColour (theme::fg);
        g.fillRect (x - 1.0f, w.getY(), 2.0f, w.getHeight());
        g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 26.0f).withCentre ({ x, w.getY() + 13.0f }), 3.0f);
    }
    // where it plays (listening, or with the song)
    float progress = ctl.audio.previewProgress.load();
    if (progress < 0.0f && ctl.audio.audioSlot.load() == row && ctl.playing) progress = ctl.audio.audioProgress.load();
    if (progress >= 0.0f)
    {
        g.setColour (theme::play);
        g.fillRect (frameToX ((double) c.start + progress * (double) (c.end - c.start)) - 1.0f, w.getY(), 2.0f, w.getHeight());
    }
    // the numbers: the tempo, the beats it holds and the beats it plays
    auto info = waveRect().withY (waveRect().getBottom() + 6).withHeight (26);
    g.setColour (theme::dim);
    g.setFont (font (13.0f));
    const int beats = c.beats();
    g.drawText (juce::String (c.seconds(), 2) + " s  ·  " + juce::String (c.lengthBeats(), 2) + juce::String::fromUTF8 (" temps à ce tempo  ·  joue ")
                    + juce::String (beats) + " temps" + (beats % 4 == 0 ? " (" + juce::String (beats / 4) + (beats == 4 ? " mesure)" : " mesures)") : juce::String()),
                info, juce::Justification::centredLeft, true);
    auto tempoBox = slowerFine.getBounds().getUnion (fasterFine.getBounds());
    tempoBox = juce::Rectangle<int> (slowerFine.getRight() + 4, tempoBox.getY(), fasterFine.getX() - slowerFine.getRight() - 8, tempoBox.getHeight());
    g.setColour (theme::panel);
    g.fillRoundedRectangle (tempoBox.toFloat(), theme::radius);
    g.setColour (theme::fg);
    g.setFont (font (18.0f, true));
    g.drawText (juce::String (c.bpm, 1) + " BPM", tempoBox, juce::Justification::centred);
    g.setColour (theme::dim);
    g.setFont (font (12.0f));
    g.drawText ("Tempo du sample", tempoBox.withY (tempoBox.getY() - 20).withHeight (18), juce::Justification::centredLeft);
    g.drawText (juce::String::fromUTF8 ("Début"), startLeft.getBounds().withY (startLeft.getY() - 20).withHeight (18).withWidth (120), juce::Justification::centredLeft);
    g.drawText ("Fin", endLeft.getBounds().withY (endLeft.getY() - 20).withHeight (18).withWidth (120), juce::Justification::centredLeft);
}

void AudioEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (36);
    ok.setBounds (top.removeFromRight (90));
    r.removeFromTop (8);
    r.removeFromTop (waveRect().getHeight() + 6 + 26 + 4);
    // a wide and short screen (a phone on its side): the controls in two columns
    const bool twoColumns = wide();
    const int bh = twoColumns ? 36 : 40, g = 6;
    auto left = twoColumns ? r.removeFromLeft (r.getWidth() / 2 - 6) : r;
    auto right = twoColumns ? r.withTrimmedLeft (12) : r;
    auto rowOf = [&] (juce::Rectangle<int>& area, int extraTop)
    {
        area.removeFromTop (extraTop);
        auto x = area.removeFromTop (bh);
        area.removeFromTop (g);
        if (! twoColumns) right = left = area;          // (one column: the next row under it)
        return x;
    };
    {   // the start and the end, the zoom
        auto x = rowOf (left, 20);
        const int w = juce::jmin (56, x.getWidth() / 8);
        startLeft.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4);
        startRight.setBounds (x.removeFromLeft (w)); x.removeFromLeft (12);
        endLeft.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4);
        endRight.setBounds (x.removeFromLeft (w)); x.removeFromLeft (12);
        zoom.setBounds (x.removeFromLeft (juce::jmin (220, x.getWidth())));
    }
    {   // the tempo: -1 -0.1 [BPM] +0.1 +1 TAP
        auto x = rowOf (left, 20);
        const int w = juce::jmin (60, x.getWidth() / 8);
        slower.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4);
        slowerFine.setBounds (x.removeFromLeft (w));
        tap.setBounds (x.removeFromRight (juce::jmin (90, x.getWidth() / 5))); x.removeFromRight (8);
        faster.setBounds (x.removeFromRight (w)); x.removeFromRight (4);
        fasterFine.setBounds (x.removeFromRight (w));
    }
    {   // half / twice, and the end on the beats
        auto x = rowOf (right, twoColumns ? 20 : 0);
        const int w = juce::jmin (60, x.getWidth() / 8);
        half.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4);
        twice.setBounds (x.removeFromLeft (w)); x.removeFromLeft (8);
        snap.setBounds (x.removeFromLeft (juce::jmin (260, x.getWidth())));
    }
    {
        auto x = rowOf (right, 0);
        stretch.setBounds (x.removeFromLeft (x.getWidth() / 2 - 3));
        x.removeFromLeft (6);
        varispeed.setBounds (x);
    }
    {
        auto x = rowOf (right, 0);
        const int w = (x.getWidth() - 2 * g) / 3;
        play.setBounds (x.removeFromLeft (w)); x.removeFromLeft (g);
        launch.setBounds (x.removeFromLeft (w)); x.removeFromLeft (g);
        songTempo.setBounds (x);
    }
    peaksFor = -1;
}

void AudioEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto w = waveRect().toFloat().expanded (0, 10);
    const auto p = e.position;
    dragging = 0;
    if (! w.contains (p)) return;
    const auto c = clip();
    const float ds = std::abs (p.x - frameToX ((double) c.start)), de = std::abs (p.x - frameToX ((double) c.end));
    dragging = ds <= de ? 1 : 2;                         // the nearer handle (a tap anywhere moves it there)
    mouseDrag (e);
}
void AudioEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == 0 || sample == nullptr) return;
    const int64_t n = sample->buffer.getNumSamples();
    const auto f = (int64_t) std::llround (xToFrame (e.position.x));   // (finer: the zoom, or the < > buttons)
    if (dragging == 1) edit ([f] (AudioClip& a) { a.start = juce::jlimit ((int64_t) 0, a.end - 1, f); });
    else edit ([f, n] (AudioClip& a) { a.end = juce::jlimit (a.start + 1, n, f); });
}
void AudioEditor::mouseUp (const juce::MouseEvent&) { dragging = 0; }

void AudioEditor::timerCallback()
{
    if (listening && ctl.audio.previewProgress.load() < 0.0f) listening = false;
    repaint (waveRect().expanded (2));
}
} // namespace sloop
