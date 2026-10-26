/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ANALOG: a classic subtractive voice. SLOOP Touch made it a two-oscillator synth in the spirit of the small
 * free soft synths everyone liked (TAL-NoiseMaker's kind), on four EDIT pages:
 *
 *   OSC   WAV1 WAV2 DTN MIX        two band-limited oscillators (saw / square / tri / sine / PWM), each its own wave,
 *                                  osc 2 detuned in cents, the mix of the two
 *   SRC   OCT2 SYNC SUB NOIS       osc 2's interval (semitones), osc 2 hard-synced to osc 1, a square sub-oscillator
 *                                  an octave below osc 1, white noise
 *   FLT   TYPE CUT RES DRV         LP24 (a four-pole ladder, after Zavalishin's TPT ladder, its input saturating),
 *                                  or the state-variable filter's LP12 / BP / HP; the drive before it
 *   FENV  F.ATK F.DEC F.SUS F.AMT  the filter's own envelope (its release: the amp's REL) and how far it opens it
 *
 * The amplitude is the track's ADSR as every engine's; the track's LFO > FLT and accent still move the cutoff.
 * (The FM-1's ANALOG had one wave for both oscillators, a 12 dB filter and the amp envelope on the filter.) */
#include <math.h>

static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM"};
static const char *const N_ANALOG_FTYPE[] = {"LP24", "LP12", "BP", "HP"};
static const char *const N_ANALOG_SYNC[] = {"OFF", "ON"};

/* the per-voice state that does not fit in voice_t's s[]: the ladder (float) and the filter envelope */
typedef struct { float lad[4]; float env; uint8_t att; } analog_vs_t;
static analog_vs_t analog_vs[NPART][NVOICE];
static analog_vs_t *analog_state(const track_t *t, const voice_t *v)
{
    uint32_t p = (uint32_t)(t - trk), i = (uint32_t)(v - t->v);
    return &analog_vs[p % NPART][i % NVOICE];
}

static void analog_note_on(track_t *t, voice_t *v)
{
    analog_vs_t *s = analog_state(t, v);
    if (!v->env && !v->env_out) {
        v->ph[0] = 0;                                 /* a fresh note: from phase 0 (an 808 starts the same every time) */
        v->ph[2] = 0;
        s->lad[0] = s->lad[1] = s->lad[2] = s->lad[3] = 0.0f;
        s->env = 0.0f;
        v->s[0] = v->s[1] = 0;                        /* the SVF */
    }
    v->ph[1] = v->ph[0] + 0x40000000u;
    s->att = 1;                                       /* the filter envelope starts again (from where it is) */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

/* the SVF's outputs (dsp.c tsvf_lp, written out with its taps: Simper's trapezoidal state-variable filter): LP12
 * as tsvf_lp, 1 BP its band-pass v1, 2 HP in - k v1 - v2 */
static inline int32_t analog_tap(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t ft)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return ft == 0u ? v2 : ft == 1u ? v1 : in - ((k * v1) >> 12) - v2;
}
#define ANALOG_K(res) (8192 - (res) * 7600 / 127)     /* tsvf_coef's damping, Q12 */

static inline int32_t analog_wave(uint32_t w, uint32_t ph, uint32_t inc, uint32_t pw)
{
    switch (w) {
    case 1: return osc_pulse(ph, inc, 0x80000000u);
    case 2: return osc_tri(ph);
    case 3: return osc_sine(ph);
    case 4: return osc_pulse(ph, inc, pw);
    default: return osc_saw(ph, inc);
    }
}

static inline float analog_sat(float x) { return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }   /* ~tanh */

/* an envelope time: 0..127 -> 1 ms .. ~8 s, as a per-block coefficient (the fraction left after a block) */
static float analog_coef(int32_t v)
{
    float secs = 0.001f * powf(8000.0f, (float)clamp(v, 0, 127) / 127.0f);
    return expf(-(float)CTL / (secs * (float)FS));
}

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    analog_vs_t *st = analog_state(t, v);
    uint32_t w1 = (uint32_t)p[P_E0] % 5u, w2 = (uint32_t)p[P_E1] % 5u, i;
    int32_t det = p[P_E2], mix = p[P_E3], oct2 = p[P_E4], sync = p[P_E5], sub = p[P_E6], noise = p[P_E7];
    uint32_t ft = (uint32_t)p[P_E8] & 3u;
    int32_t res = p[P_E10], drv = p[P_E11];
    float fe;
    /* the filter envelope, once a block: attack to the top, decay to SUS while the key is held, then the amp's REL */
    if (!v->gate)
        st->env *= analog_coef(p[P_REL]), st->att = 0;
    else if (st->att) {
        st->env = 1.25f - (1.25f - st->env) * analog_coef(p[P_E12]);   /* an RC charge aimed past the top */
        if (st->env >= 1.0f)
            st->env = 1.0f, st->att = 0;
    } else {
        float sus = (float)p[P_E14] / 127.0f;
        st->env = sus + (st->env - sus) * analog_coef(p[P_E13]);
    }
    fe = st->env;
    {
        int32_t cut = (p[P_E9] << 8) + m->cutoff + (64 * (v->pitch16 - 60 * 16) >> 4)   /* half key tracking */
                      + (int32_t)(fe * (float)(p[P_E15] * 2 * 256));
        int32_t drive = 32768 + drv * 768;                          /* 1x .. 4x */
        int32_t dw = drv * 258;                                     /* DRV: dry -> driven (a clean range low) */
        uint32_t inc1 = m->inc, inc2, incs = m->inc >> 1;
        /* DTN in cents, OCT2 in semitones: whole 1/16 semitones from the table, the rest as a fine factor */
        int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;
        uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
        int32_t m2 = mix * 258, m1 = 32767 - m2;
        int32_t sb = sub * 200, nz = noise * 200;
        uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
        int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
        float l0 = st->lad[0], l1 = st->lad[1], l2 = st->lad[2], l3 = st->lad[3];
        float g = 0, G = 0, k = 0;
        tsvf_t flt;
        int32_t fk = ANALOG_K(res);
        inc2 = pitch_inc(clamp(m->pitch16 + oct2 * 16 + d16, 0, 2047));
        inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
        if (m->fine)
            inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine);
        cut = clamp(cut, 0, 127 << 8);
        if (ft == 0u) {                               /* the ladder: g = tan(pi fc / FS), fc 30 Hz .. 16 kHz */
            float hz = 30.0f * powf(16000.0f / 30.0f, (float)cut / (float)(127 << 8));
            if (hz > 0.45f * (float)FS)
                hz = 0.45f * (float)FS;
            g = tanf(3.14159265f * hz / (float)FS);
            G = g / (1.0f + g);
            k = (float)res * (4.1f / 127.0f);         /* past 4: it sings on its own */
        } else
            tsvf_coef(&flt, cut, res);
        for (i = 0; i < n; i++) {
            int32_t a = analog_wave(w1, ph0, inc1, pw), b = m2 ? analog_wave(w2, ph1, inc2, pw) : 0, s;
            uint32_t wrap = ph0 + inc1 < ph0;
            ph0 += inc1;
            ph1 = sync && wrap ? (uint32_t)((uint64_t)ph0 * inc2 / (inc1 ? inc1 : 1u)) : ph1 + inc2;   /* SYNC: osc 2 restarts with osc 1 */
            ph2 += incs;
            s = m2 ? mulq15(a, m1) + mulq15(b, m2) : a;  /* (MIX 0, the 808s and subs: one oscillator) */
            if (sb)
                s += mulq15(osc_pulse(ph2, incs, 0x80000000u), sb);
            if (nz)
                s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
            if (drv)                                  /* pre-shifts: drive is up to 4x, no overflow */
                s += mulq15(softclip(((s >> 2) * (drive >> 2)) >> 11) - s, dw);
            if (ft == 0u) {
                /* the TPT ladder: four one-poles, the feedback solved for this sample, the input saturating */
                float x = (float)s * (1.0f / 32768.0f) * (1.0f + 0.35f * k);   /* (the bass a resonant ladder loses) */
                float G2 = G * G, S = G2 * G * l0 * (1.0f - G) + G2 * l1 * (1.0f - G) + G * l2 * (1.0f - G) + l3 * (1.0f - G);
                float y4 = (G2 * G2 * x + S) / (1.0f + k * G2 * G2), u = analog_sat(x - k * y4), y, vv;
                vv = G * (u - l0); y = vv + l0; l0 = y + vv; u = y;
                vv = G * (u - l1); y = vv + l1; l1 = y + vv; u = y;
                vv = G * (u - l2); y = vv + l2; l2 = y + vv; u = y;
                vv = G * (u - l3); y = vv + l3; l3 = y + vv;
                s = (int32_t)(analog_sat(y) * 32767.0f);
            } else {   /* the SVF: linear up to half scale, then a soft knee (only resonance peaks saturate) */
                int32_t y = analog_tap(&flt, fk, s >> 1, &ic1, &ic2, ft - 1u), aa = y < 0 ? -y : y;
                if (aa > 16000) {
                    aa = 16000 + (softclip((aa - 16000) * 2) >> 1);
                    y = y < 0 ? -aa : aa;
                }
                s = y << 1;
            }
            out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
        }
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        v->s[0] = ic1;
        v->s[1] = ic2;
        v->s[2] = nst;
        /* (a denormal or a blown-up state never stays: the ladder is reset when it leaves the audio range) */
        if (!(l3 > -8.0f && l3 < 8.0f))
            l0 = l1 = l2 = l3 = 0.0f;
        st->lad[0] = l0; st->lad[1] = l1; st->lad[2] = l2; st->lad[3] = l3;
    }
}

/* the presets: {WAV1 WAV2 DTN MIX OCT2 SYNC SUB NOIS} E2(TYPE CUT RES DRV F.ATK F.DEC F.SUS F.AMT), {A D S R} */
#define AN(w1, w2, dt, mx, o2, sy, sb, nz) {w1, w2, dt, mx, o2, sy, sb, nz}
static const preset_t ANALOG_PRESETS[] = {
    /* 808s: a sine that decays (SUS 0) with a little pitch drop (ENV -> PITCH) and slides (GLIDE, legato) */
    {"808 BOOM", AN(3, 3, 0, 0, 0, 0, 0, 0), {0, 112, 0, 52}, 0, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 59, P_GLMODE + 1, 1, P_ED_PIT + 1, 18, P_TRANS + 1, -24), E2(1, 127, 0, 48, 0, 0, 127, 0)},
    {"808 DIRTY", AN(3, 3, 0, 0, 0, 0, 0, 0), {0, 108, 0, 46}, 0, 1, FX(24, 0, 0, 0),
     XP(P_GLIDE + 1, 56, P_GLMODE + 1, 1, P_ED_PIT + 1, 24, P_TRANS + 1, -24), E2(1, 96, 12, 118, 0, 0, 127, 0)},
    {"SUB BASS", AN(3, 3, 0, 0, 0, 0, 0, 0), {0, 60, 118, 24}, 0, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 40, P_TRANS + 1, -24), E2(1, 127, 0, 10, 0, 0, 127, 0)},
    {"808 SLIDE", AN(3, 3, 0, 0, 0, 0, 0, 0), {0, 118, 0, 60}, 0, 1, FX(30, 0, 0, 0),
     XP(P_GLIDE + 1, 82, P_GLMODE + 1, 1, P_ED_PIT + 1, 12, P_TRANS + 1, -24), E2(1, 127, 0, 72, 0, 0, 127, 0)},
    /* acid: one saw into the ladder, high resonance, a quick filter envelope, slides where notes overlap */
    {"ACID 303", AN(0, 0, 0, 0, 0, 0, 0, 0), {0, 70, 100, 20}, 0, 1, FX(28, 0, 22, 10),
     XP(P_GLIDE + 1, 34, P_GLMODE + 1, 1, P_TRANS + 1, -12), E2(0, 30, 104, 44, 0, 52, 0, 52)},
    {"PLUGG BASS", AN(2, 2, 0, 0, 0, 0, 0, 0), {0, 96, 92, 34}, 0, 1, FX(0, 0, 0, 6),
     XP(P_GLIDE + 1, 52, P_GLMODE + 1, 1, P_TRANS + 1, -24), E2(1, 92, 0, 30, 0, 0, 127, 0)},
    /* drum & bass: two detuned saws, the filter moving slowly */
    {"REESE", AN(0, 0, 22, 64, 0, 0, 30, 0), {0, 70, 118, 28}, 0, 1, FX(0, 22, 0, 6),
     XP(P_LRATE + 1, 22, P_LD_FLT + 1, 10, P_TRANS + 1, -24), E2(0, 56, 30, 36, 0, 70, 118, 0)},
    {"WOBBLE", AN(0, 1, 9, 64, 0, 0, 40, 0), {0, 64, 127, 22}, 0, 1, FX(10, 0, 0, 4),
     XP(P_LRATE + 1, 74, P_LD_FLT + 1, 40, P_TRANS + 1, -24), E2(0, 38, 80, 62, 0, 64, 127, 0)},
    {"FUNK BASS", AN(1, 1, 6, 50, 0, 0, 0, 0), {0, 55, 62, 20}, 0, 1, FX(0, 0, 0, 4),
     XP(P_GLIDE + 1, 50, P_GLMODE + 1, 1, P_TRANS + 1, -24), E2(1, 50, 30, 22, 0, 55, 62, 30)},
    {"G-FUNK LD", AN(2, 2, 0, 0, 0, 0, 0, 0), {6, 70, 112, 46}, 0, 1, FX(0, 0, 28, 30),
     XP(P_GLIDE + 1, 74, P_LD_PIT + 1, 2, P_LRATE + 1, 89, P_LFADE + 1, 50), E2(1, 92, 0, 10, 0, 0, 127, 0)},
    {"TRAP PLUCK", AN(0, 0, 8, 64, 0, 0, 0, 0), {0, 80, 24, 45}, 0, 0, FX(0, 15, 34, 26), XP(0), E2(1, 52, 35, 20, 0, 80, 24, 48)},
    {"SYN BRASS", AN(0, 0, 10, 64, 0, 0, 0, 0), {12, 70, 96, 35}, 0, 0, FX(0, 15, 12, 24), XP(0), E2(0, 45, 20, 16, 12, 70, 96, 34)},
    /* trance / EDM: eight detuned saw voices on one note */
    {"SUPERSAW", AN(0, 0, 14, 64, 0, 0, 0, 0), {4, 70, 112, 46}, 0, 1, FX(0, 20, 30, 36),
     XP(P_VOICE + 1, 3, P_DETUNE + 1, 64, P_GLIDE + 1, 30), E2(1, 96, 12, 10, 4, 70, 112, 10)},
    {"WARM PAD", AN(4, 4, 12, 64, 0, 0, 0, 6), {75, 90, 115, 90}, 0, 0, FX(0, 55, 20, 55),
     XP(P_LRATE + 1, 30, P_LD_SHP + 1, 20), E2(0, 52, 8, 0, 75, 90, 115, 0)},
    {"DARK STR", AN(0, 0, 18, 64, 0, 0, 0, 0), {60, 90, 118, 85}, 0, 0, FX(0, 50, 18, 60), XP(0), E2(0, 48, 6, 0, 60, 90, 118, 0)},
    {"ATMOS PAD", AN(4, 4, 25, 64, 0, 0, 0, 20), {90, 100, 120, 100}, 0, 0, FX(0, 60, 30, 75),
     XP(P_LRATE + 1, 18, P_LD_PIT + 1, 1), E2(1, 60, 15, 0, 90, 100, 120, 0)},
    {"PAN FLUTE", AN(2, 2, 0, 0, 0, 0, 0, 30), {14, 70, 110, 40}, 0, 1, FX(0, 10, 24, 45),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 80, P_LFADE + 1, 70), E2(1, 70, 10, 0, 0, 0, 127, 0)},
    {"FAT LEAD", AN(0, 1, 8, 64, 0, 0, 40, 0), {2, 70, 110, 30}, 0, 1, FX(0, 0, 26, 20), XP(P_GLIDE + 1, 40),
     E2(0, 70, 34, 30, 2, 70, 110, 30)},
    {"MONO BASS", AN(0, 0, 4, 64, 0, 0, 50, 0), {0, 60, 70, 20}, 0, 1, FX(10, 0, 0, 4), XP(P_TRANS + 1, -24),
     E2(0, 48, 40, 30, 0, 60, 70, 44)},
    {"PWM LEAD", AN(4, 4, 0, 0, 0, 0, 0, 0), {2, 70, 110, 30}, 0, 1, FX(0, 10, 26, 22),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 80, P_LFADE + 1, 60, P_GLIDE + 1, 30), E2(1, 80, 20, 10, 0, 0, 127, 0)},
    {"SAW BASS", AN(0, 0, 6, 64, 0, 0, 30, 0), {0, 60, 60, 20}, 0, 1, FX(8, 0, 0, 4), XP(P_TRANS + 1, -24),
     E2(0, 52, 34, 40, 0, 60, 60, 30)},
    {"SINE PAD", AN(3, 3, 10, 64, 0, 0, 0, 4), {80, 90, 118, 90}, 0, 0, FX(0, 60, 20, 65), XP(0), E2(1, 90, 0, 0, 0, 0, 127, 0)},
    /* SLOOP Touch: what the two waves, the sub, the sync and the ladder bring */
    {"SAW+SUB BS", AN(0, 1, 0, 30, -12, 0, 90, 0), {0, 80, 100, 18}, 0, 1, FX(6, 0, 0, 0), XP(P_TRANS + 1, -24),
     E2(0, 36, 50, 30, 0, 40, 30, 46)},
    {"SYNC LEAD", AN(0, 0, 0, 100, 7, 1, 0, 0), {0, 70, 110, 30}, 0, 1, FX(0, 10, 30, 20),
     XP(P_GLIDE + 1, 30, P_LRATE + 1, 30, P_LD_PIT + 1, 1), E2(0, 84, 30, 40, 0, 60, 60, 30)},
    {"JUNO PAD", AN(4, 0, 6, 40, 0, 0, 60, 0), {50, 90, 110, 80}, 0, 0, FX(0, 0, 18, 50),
     XP(P_LRATE + 1, 24, P_LD_SHP + 1, 26, P_MTYPE + 1, 2), E2(0, 58, 24, 0, 50, 90, 90, 18)},
    {"JUNO STRING", AN(0, 0, 0, 0, 0, 0, 0, 0), {40, 90, 118, 70}, 0, 0, FX(0, 0, 10, 40), XP(P_MTYPE + 1, 1),
     E2(1, 70, 10, 0, 40, 90, 110, 10)},
    {"VIBE KEYS", AN(4, 2, 0, 30, 12, 0, 0, 0), {0, 90, 50, 50}, 0, 0, FX(0, 0, 20, 30),
     XP(P_MTYPE + 1, 4, P_MRATE + 1, 70, P_MDEPTH + 1, 50, P_MRISE + 1, 40), E2(1, 76, 10, 0, 0, 80, 60, 20)},
    {"VB LEAD", AN(0, 1, 6, 40, 0, 0, 30, 0), {4, 70, 110, 30}, 0, 1, FX(0, 0, 26, 24),
     XP(P_MTYPE + 1, 4, P_MRATE + 1, 76, P_MDEPTH + 1, 64, P_MRISE + 1, 70), E2(0, 74, 26, 20, 0, 70, 90, 24)},
    {"POLY BRASS", AN(0, 0, 9, 64, 0, 0, 30, 0), {10, 70, 104, 34}, 0, 0, FX(0, 30, 14, 26), XP(0), E2(0, 40, 22, 20, 8, 66, 70, 42)},
    {"SQR SYNC BS", AN(1, 0, 0, 80, 12, 1, 60, 0), {0, 60, 90, 16}, 0, 1, FX(10, 0, 0, 0), XP(P_TRANS + 1, -24),
     E2(0, 42, 40, 50, 0, 50, 40, 40)},
    {"RESO SWEEP", AN(0, 0, 10, 64, 0, 0, 0, 10), {30, 100, 110, 80}, 0, 0, FX(0, 30, 30, 50),
     XP(P_LRATE + 1, 8, P_LD_FLT + 1, 34), E2(0, 40, 112, 20, 90, 100, 110, 26)},
    {"HOOVER", AN(4, 0, 24, 64, 0, 0, 70, 0), {0, 80, 110, 40}, 0, 1, FX(20, 40, 20, 30),
     XP(P_GLIDE + 1, 60, P_VOICE + 1, 3, P_DETUNE + 1, 70, P_ED_PIT + 1, -20), E2(0, 70, 20, 50, 0, 70, 90, 20)},
};

static const engine_t ENG_ANALOG = {
    "ANALOG", {"OSC", "SRC", "FLT", "FENV"},
    {
        {"WAV1", F_ENUM, 0, 4, 0, N_ANALOG_WAVE, 0},
        {"WAV2", F_ENUM, 0, 4, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"OCT2", F_INT, -24, 24, 0, 0, "st"},
        {"SYNC", F_ENUM, 0, 1, 0, N_ANALOG_SYNC, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"TYPE", F_ENUM, 0, 3, 0, N_ANALOG_FTYPE, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"F.ATK", F_INT, 0, 127, 0, 0, 0},
        {"F.DEC", F_INT, 0, 127, 60, 0, 0},
        {"F.SUS", F_PCT, 0, 127, 64, 0, 0},
        {"F.AMT", F_BIPCT, -64, 63, 20, 0, 0},
    },
    ANALOG_PRESETS, sizeof(ANALOG_PRESETS) / sizeof(ANALOG_PRESETS[0]), 2, analog_note_on, analog_render,
    0xF986, {P_E9, P_E10, P_E15, P_E11},
};
