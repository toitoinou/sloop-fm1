// SPDX-License-Identifier: GPL-3.0-only
// The OBXD engine's DSP: Obxd's voice (Vadim Filatov, 2013-2014: third_party/obxd), one ObxdVoice per SLOOP
// voice (3 synth parts x 8 voices), behind a small C API for eng_obxd.c. Only the voice: Obxd's own voice
// allocation, LFO and plugin are SLOOP's here (its voices, its LFO, its effects). Its amplitude envelope stays open
// (SLOOP's ADSR shapes the note); its filter envelope, its two oscillators and its filter are Obxd's.
#include "ObxdVoice.h"
#include "obxd_glue.h"
#include <cstring>

namespace
{
constexpr int kSlots = OBXD_PARTS * OBXD_VOICES;
struct Slot
{
    ObxdVoice voice;
    bool gate = false;
};
Slot* slots()
{
    static Slot* s = new Slot[kSlots];
    return s;
}
float logscale (float p, float lo, float hi, float roll) { return logsc (std::min (std::max (p, 0.0f), 1.0f), lo, hi, roll); }
} // namespace

extern "C" void obxd_init (void)
{
    auto* s = slots();
    for (int i = 0; i < kSlots; ++i)
    {
        auto& v = s[i].voice;
        v.setSampleRate (44100.0f);
        v.setHQ (false);
        v.legatoMode = 3;                           // a note on a sounding voice: both envelopes start again
        v.porta = logscale (1.0f, 0.14f, 250.0f, 150.0f);   // (no portamento: SLOOP's glide moves the pitch)
        v.env.setAttack (4.0f);                     // the amplitude: open while the voice sounds (SLOOP's ADSR)
        v.env.setDecay (1000.0f);
        v.env.setSustain (1.0f);
        v.env.setRelease (60000.0f);
        v.vamp = 0.0f;
        v.vflt = 0.3f;                              // the velocity opens the filter a little
        v.fltKF = 0.3f;
        v.setBrightness (26000.0f);
        v.setEnvDer (0.25f);                        // Obxd's "analog" spread between voices
        v.FltDetAmt = 3.0f;
        v.PortaDetuneAmt = 0.1f;
        v.levelDetuneAmt = 0.08f;
        v.osc.totalDetune = logscale (0.15f, 0.001f, 0.90f, 19.0f);
        v.osc.oct = 0;
        v.osc.tune = 0.0f;
        v.osc.osc1p = 0.0f;
    }
}

extern "C" void obxd_note_on (int slot, int note, int velocity)
{
    if (slot < 0 || slot >= kSlots) return;
    auto& s = slots()[slot];
    s.voice.NoteOn (std::min (std::max (note, 0), 127), (float) std::min (std::max (velocity, 1), 127) / 127.0f);
    s.gate = true;
}

extern "C" void obxd_render (int slot, const obxd_in_t* in, int16_t* out, int n)
{
    if (slot < 0 || slot >= kSlots) { std::memset (out, 0, sizeof (int16_t) * (size_t) n); return; }
    auto& s = slots()[slot];
    auto& v = s.voice;
    if (s.gate && ! in->gate) { v.NoteOff(); s.gate = false; }
    auto& o = v.osc;
    o.osc1Saw = in->osc1 != 1;
    o.osc1Pul = in->osc1 >= 1;
    const int w2 = in->osc2 % 3;
    o.osc2Saw = w2 != 1;
    o.osc2Pul = w2 >= 1;
    o.hardSync = in->osc2 >= 3;
    o.osc2p = in->osc2_pitch;
    o.osc2Det = logscale (in->detune, 0.001f, 0.6f, 19.0f);
    o.o1mx = std::min (1.0f, 2.0f * (1.0f - in->mix));
    o.o2mx = std::min (1.0f, 2.0f * in->mix);
    o.nmx = logscale (in->noise, 0.0f, 1.0f, 35.0f);
    o.pulseWidth = in->pw * 0.95f;
    o.xmod = in->xmod * 24.0f;
    v.cutoff = in->cutoff * 120.0f;
    v.flt.setResonance (0.991f - logscale (1.0f - in->reso, 0.0f, 0.991f, 40.0f));
    v.flt.setMultimode (std::min (std::max (in->multimode, 0.0f), 1.0f));
    v.fourpole = in->four_pole != 0;
    v.fenvamt = in->fenv_amt * 140.0f;
    v.fenv.setAttack (logscale (in->f_attack, 1.0f, 60000.0f, 900.0f));
    v.fenv.setDecay (logscale (in->f_decay, 1.0f, 60000.0f, 900.0f));
    v.fenv.setSustain (in->f_sustain);
    v.fenv.setRelease (logscale (in->f_release, 1.0f, 60000.0f, 900.0f));
    // the pitch: the note Obxd plays, and the rest (glide, bend, LFO, unison detune) through its vibrato input
    const float semis = in->note - (float) v.midiIndx;
    for (int i = 0; i < n; ++i)
    {
        v.lfoIn = 0.0f;
        v.lfoVibratoIn = semis;
        const float x = v.ProcessSample() * 0.5f;
        out[i] = (int16_t) std::max (-32767.0f, std::min (32767.0f, x * 32767.0f));
    }
}
