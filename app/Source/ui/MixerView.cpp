// SPDX-License-Identifier: GPL-3.0-only
#include "ui/MixerView.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

juce::Rectangle<int> MixerView::stripRect (int strip) const
{
    if (strip == kMaster) return getLocalBounds().removeFromRight (masterW());
    const int v = column (strip) - first, n = shown(), w = (getWidth() - masterW() - 8) / n;
    if (column (strip) < 0 || v < 0 || v >= n) return {};
    return { v * w, 0, w - 4, getHeight() };
}

juce::Rectangle<int> MixerView::partRect (int strip, Part part) const
{
    auto r = stripRect (strip);
    if (r.isEmpty()) return {};
    r = r.reduced (4, 4);
    const bool master = strip == kMaster;
    auto top = r.removeFromTop (30);
    if (part == name) return master && shown() < strips() ? top.withTrimmedRight (top.getWidth() / 2) : top;
    if (master && shown() < strips())
    {
        auto pages = top.withTrimmedLeft (top.getWidth() / 2);
        if (part == pagePrev) return pages.removeFromLeft (pages.getWidth() / 2).reduced (1);
        if (part == pageNext) return pages.withTrimmedLeft (pages.getWidth() / 2).reduced (1);
    }
    r.removeFromTop (4);
    auto ms = r.removeFromTop (28);
    if (master) return part == record ? ms : juce::Rectangle<int>();   // (the master: the output's recording there)
    if (part == mute) return ms.removeFromLeft (ms.getWidth() / 2 - 2);
    if (part == solo) return ms.withTrimmedLeft (ms.getWidth() / 2 + 2);
    r.removeFromTop (6);
    for (auto p : { pan, rev, dly })
    {
        auto row = r.removeFromTop (24);
        r.removeFromTop (4);
        if (part == p) return row;
    }
    r.removeFromTop (4);
    return part == fader ? r : juce::Rectangle<int>();
}

MixerView::Control MixerView::control (int strip, Part part) const
{
    Control c;
    auto& k = const_cast<Controller&> (ctl);
    auto global = [&c, &k] (int id, const char* label)
    {
        c.valid = se_global_describe (id, &c.min, &c.max, &c.def, nullptr) != 0;
        c.value = k.globals[(size_t) id];
        c.label = label;
        c.set = [&k, id] (int v) { k.setGlobal (id, v); };
    };
    if (strip == kMaster)                               // the master: mute / solo are not there; pan rev dly: the bus
    {
        if (part == pan) global (SE_G_FILT, "FILTRE");
        if (part == rev) global (SE_G_DUST, "DUST");
        if (part == dly) global (SE_G_DUCK, "DUCK");
        if (part == fader)
        {
            c = { k.masterVolume, 0, 127, 100, [&k] (int v) { k.setMasterVolume (v); }, "VOL", true };
        }
        return c;
    }
    if (strip == kAudio)                                 // the audio track: the app's own mix (no send)
    {
        if (part == pan) c = { k.audioPan, -64, 63, 0, [&k] (int v) { k.setAudioMix (k.audioLevel, v, k.audioMuted); }, "PAN", true };
        if (part == fader) c = { k.audioLevel, 0, 127, 90, [&k] (int v) { k.setAudioMix (v, k.audioPan, k.audioMuted); }, "LVL", true };
        return c;
    }
    auto param = [&c, &k, strip] (int id, const char* label)
    {
        c.valid = se_describe (isDrum (strip) ? -1 : k.sounds[(size_t) strip].engine, id, &c.min, &c.max, &c.def, nullptr) != 0;
        c.value = k.params[(size_t) strip][(size_t) id];
        c.label = label;
        c.set = [&k, strip, id] (int v) { k.setTrackParam (strip, id, v); };
    };
    if (isDrum (strip))                                  // (SLOOP: the drum track's level and sends are global)
    {
        if (part == rev) global (SE_G_DRREV, "REV");
        if (part == dly) global (SE_G_DRDLY, "DLY");
        if (part == fader) global (SE_G_DRLVL, "LVL");
        return c;
    }
    if (part == pan) param (SE_P_PAN, "PAN");
    if (part == rev) param (SE_P_REV, "REV");
    if (part == dly) param (SE_P_DLY, "DLY");
    if (part == fader) param (SE_P_LEVEL, "LVL");
    return c;
}

MixerView::Hit MixerView::hitAt (juce::Point<int> p) const
{
    for (int strip = 0; strip <= kMaster; ++strip)
        for (auto part : { name, mute, solo, pan, rev, dly, fader, pagePrev, pageNext, record })
            if (partRect (strip, part).contains (p)) return { strip, part };
    return {};
}

void MixerView::timerCallback()
{
    // the meters: the loudest since the last look, falling back slowly (about 20 dB a second)
    for (int t = 0; t < kTracks; ++t)
    {
        const float level = juce::jlimit (0.0f, 1.0f, (float) ctl.audio.peak[t].exchange (0) / 32768.0f);   // (Q15)
        meter[(size_t) t] = juce::jmax (level, meter[(size_t) t] * 0.86f);
    }
    meter[kAudio] = juce::jmax (juce::jlimit (0.0f, 1.0f, (float) ctl.audio.audioPeak.exchange (0) / 32768.0f), meter[kAudio] * 0.86f);
    meter[kMaster] = juce::jmax (juce::jlimit (0.0f, 1.0f, ctl.audio.masterPeak.exchange (0.0f)), meter[kMaster] * 0.86f);
    if (isVisible()) repaint();
}

void MixerView::paint (juce::Graphics& g)
{
    first = juce::jlimit (0, juce::jmax (0, strips() - shown()), first);   // (tracks taken away)
    for (int strip = 0; strip <= kMaster; ++strip)
    {
        auto box = stripRect (strip);
        if (box.isEmpty()) continue;
        const bool master = strip == kMaster;
        const auto col = master ? theme::fg : theme::track (strip);
        g.setColour (! master && strip == ctl.sel ? theme::cellBeat : theme::panel);
        g.fillRoundedRectangle (box.toFloat(), theme::radius);
        auto nm = partRect (strip, name);
        g.setColour (col);
        g.setFont (font (14.0f, true));
        g.drawText (master ? juce::String (box.getWidth() < 120 ? "MST" : "MASTER") : strip == kAudio ? juce::String ("AU") : juce::String (trackId (strip)), nm.withTrimmedLeft (4), juce::Justification::centredLeft, true);
        if (master && shown() < strips())
            for (auto p : { pagePrev, pageNext })
            {
                auto r = partRect (strip, p).toFloat();
                const bool can = p == pagePrev ? first > 0 : first + shown() < strips();
                g.setColour (theme::cellBeat.withAlpha (can ? 1.0f : 0.35f));
                g.fillRoundedRectangle (r, theme::radius);
                g.setColour (theme::fg.withAlpha (can ? 1.0f : 0.35f));
                g.drawText (p == pagePrev ? "<" : ">", r, juce::Justification::centred);
            }
        if (master)                                      // REC: the output into a WAV (its time while it runs)
        {
            auto r = partRect (strip, record).toFloat();
            const bool on = ctl.audio.isRecording();
            g.setColour (on ? theme::rec : theme::cell);
            g.fillRoundedRectangle (r, theme::radius);
            g.setColour (on ? theme::ink : theme::rec);
            g.setFont (font (12.0f, true));
            const int sec = (int) ((double) ctl.audio.recordedFrames.load() / juce::jmax (1.0, ctl.audio.outputRate()));
            g.drawText (on ? juce::String::formatted ("%d:%02d", sec / 60, sec % 60) : juce::String::fromUTF8 ("● REC"), r,
                        juce::Justification::centred);
        }
        if (! master)
            for (auto p : { mute, solo })
            {
                auto r = partRect (strip, p).toFloat();
                const bool on = p == mute ? (strip == kAudio ? ctl.audioMuted : ctl.muted[(size_t) strip]) : (ctl.soloMask >> strip & 1u) != 0;
                g.setColour (on ? (p == mute ? theme::rec : juce::Colour (0xffe0c45a)) : theme::cell);
                g.fillRoundedRectangle (r, theme::radius);
                g.setColour (on ? theme::ink : theme::fg);
                g.setFont (font (12.0f, true));
                g.drawText (p == mute ? "M" : "S", r, juce::Justification::centred);
            }
        for (auto p : { pan, rev, dly })                 // a horizontal bar (pan, the filter: from the middle)
        {
            const auto c = control (strip, p);
            auto r = partRect (strip, p).toFloat();
            if (r.isEmpty() || ! c.valid) continue;
            g.setColour (theme::cell);
            g.fillRoundedRectangle (r, 3.0f);
            const float frac = (float) (c.value - c.min) / (float) juce::jmax (1, c.max - c.min);
            const bool bipolar = c.min < 0;
            g.setColour (col.withAlpha (0.7f));
            if (bipolar)
            {
                const float mid = r.getCentreX(), x = r.getX() + frac * r.getWidth();
                g.fillRect (juce::Rectangle<float> (juce::jmin (mid, x), r.getY(), std::abs (x - mid) + 1.0f, r.getHeight()));
            }
            else g.fillRoundedRectangle (r.withWidth (juce::jmax (2.0f, frac * r.getWidth())), 3.0f);
            g.setColour (theme::fg);
            g.setFont (font (10.5f, true));
            g.drawText (c.label, r.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
        }
        {                                                // the fader and the meter
            const auto c = control (strip, fader);
            auto r = partRect (strip, fader).toFloat();
            if (r.isEmpty() || ! c.valid) continue;
            auto meterR = r.removeFromRight (8.0f);
            r.removeFromRight (4.0f);
            auto valR = r.removeFromBottom (18.0f);
            const float frac = (float) (c.value - c.min) / (float) juce::jmax (1, c.max - c.min);
            auto lane = r.withSizeKeepingCentre (6.0f, r.getHeight());
            g.setColour (theme::cell);
            g.fillRoundedRectangle (lane, 3.0f);
            g.setColour (col.withAlpha (0.6f));
            g.fillRoundedRectangle (lane.withTrimmedTop (lane.getHeight() * (1.0f - frac)), 3.0f);
            auto cap = juce::Rectangle<float> (r.getWidth() - 6.0f, 12.0f).withCentre ({ r.getCentreX(), r.getY() + r.getHeight() * (1.0f - frac) });
            g.setColour (theme::fg);
            g.fillRoundedRectangle (cap.constrainedWithin (r), 3.0f);
            g.setColour (theme::cell);
            g.fillRoundedRectangle (meterR, 2.0f);
            const float m = meter[(size_t) strip];
            g.setColour (m > 0.95f ? theme::rec : theme::play);
            g.fillRoundedRectangle (meterR.withTrimmedTop (meterR.getHeight() * (1.0f - std::sqrt (m))), 2.0f);
            g.setColour (theme::dim);
            g.setFont (font (11.0f));
            g.drawText (juce::String (c.value), valR, juce::Justification::centred);
        }
    }
}

void MixerView::mouseDown (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.getPosition());
    if (h.strip < 0) return;
    if (h.part == pagePrev || h.part == pageNext)
    {
        first = juce::jlimit (0, juce::jmax (0, strips() - shown()), first + (h.part == pagePrev ? -1 : 1) * shown());
        repaint();
        return;
    }
    if (h.part == record)
    {
        if (ctl.audio.isRecording())
        {
            ctl.audio.stopRecording();
            if (onRecorded) onRecorded (recording);
        }
        else
        {
            recording = store::folder().getChildFile ("recordings")
                            .getChildFile ("SLOOP " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");
            ctl.audio.startRecording (recording);
        }
        repaint();
        return;
    }
    if (h.strip == kAudio)
    {
        if (h.part == name) return;
        if (h.part == mute) { ctl.setAudioMix (ctl.audioLevel, ctl.audioPan, ! ctl.audioMuted); return; }
        if (h.part == solo) { ctl.toggleSolo (kAudio); return; }
    }
    if (h.strip < kTracks)
    {
        if (h.part == name) { ctl.select (h.strip); return; }
        if (h.part == mute) { ctl.setMute (h.strip, ! ctl.muted[(size_t) h.strip]); return; }
        if (h.part == solo) { ctl.toggleSolo (h.strip); return; }
    }
    const auto c = control (h.strip, h.part);
    if (! c.valid) return;
    const int key = h.strip * 16 + (int) h.part;
    if (juce::Time::getMillisecondCounterHiRes() - lastTap[key] < 320.0) { c.set (c.def); lastTap[key] = 0; return; }
    lastTap[key] = juce::Time::getMillisecondCounterHiRes();
    drags[e.source.getIndex()] = { h, h.part == fader ? (float) e.getPosition().y : (float) e.getPosition().x, c.value };
}

void MixerView::mouseDrag (const juce::MouseEvent& e)
{
    auto it = drags.find (e.source.getIndex());
    if (it == drags.end()) return;
    const auto& d = it->second;
    const auto c = control (d.hit.strip, d.hit.part);
    if (! c.valid) return;
    const auto r = partRect (d.hit.strip, d.hit.part);
    const float span = (float) (c.max - c.min);
    const float moved = d.hit.part == fader ? d.start - (float) e.getPosition().y : (float) e.getPosition().x - d.start;
    const float length = (float) juce::jmax (40, d.hit.part == fader ? r.getHeight() - 18 : r.getWidth());
    c.set (juce::jlimit (c.min, c.max, d.value0 + juce::roundToInt (moved * span / length)));
}

void MixerView::mouseUp (const juce::MouseEvent& e) { drags.erase (e.source.getIndex()); }
} // namespace sloop
