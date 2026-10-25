/* SPDX-License-Identifier: GPL-3.0-only */
/* OBXD (the mobile app only, FELUCCA_OBXD): Obxd, Vadim Filatov's Oberheim OB-X voice (2013-2014), as one SLOOP
 * engine: its two oscillators (saw, pulse or both; osc 2 tuned, detuned, hard-synced, cross-modulated), its noise,
 * its multimode filter (12 dB LP -> BP -> HP, or 24 dB, 4 to 1 poles) and its filter envelope, on four EDIT pages:
 *
 *   OSC   OSC1 OSC2 PIT2 DET       MIX   MIX NOIS PW XMOD
 *   FLT   CUT RES MODE POLE        FENV  F.ATK F.DEC F.SUS F.AMT (its release: the amp's REL)
 *
 * A SLOOP voice is an Obxd voice (plaits-like: its own slot), SLOOP's ADSR and velocity its level; the track's
 * LFO > FLT, ENV > FLT and accent move CUT; Obxd's little differences between voices (its "analog" spread) stay. */
#include "obxd_glue.h"

_Static_assert(OBXD_PARTS == NPART && OBXD_VOICES == NVOICE, "obxd_glue.h: a slot for every voice");

static const char *const N_OBXD_OSC1[] = {"SAW", "PULSE", "S+P"};
static const char *const N_OBXD_OSC2[] = {"SAW", "PULSE", "S+P", "SAW SY", "PUL SY", "S+P SY"};
static const char *const N_OBXD_POLE[] = {"12 dB", "24 dB"};

static int obxd_slot(const track_t *t, const voice_t *v) { return (int)(t - trk) * OBXD_VOICES + (int)(v - t->v); }

static void obxd_note_on_v(track_t *t, voice_t *v)
{
    obxd_note_on(obxd_slot(t, v), v->note, v->vel);
}

static void obxd_render_voice(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int16_t buf[CTL];
    uint32_t i;
    obxd_in_t in;
    float rel = (float)p[P_REL] / 127.0f;
    in.note = (float)m->pitch16 / 16.0f + (float)m->fine * 0.0042272f;
    in.osc1 = clamp(p[P_E0], 0, 2);
    in.osc2 = clamp(p[P_E1], 0, 5);
    in.osc2_pitch = (float)clamp(p[P_E2], 0, 48);
    in.detune = (float)p[P_E3] / 127.0f;
    in.mix = (float)p[P_E4] / 127.0f;
    in.noise = (float)p[P_E5] / 127.0f;
    in.pw = (float)p[P_E6] / 127.0f;
    in.xmod = (float)p[P_E7] / 127.0f;
    in.cutoff = (float)((p[P_E8] << 8) + m->cutoff) / (float)(127 << 8);
    in.reso = (float)p[P_E9] / 127.0f;
    in.multimode = (float)p[P_E10] / 127.0f;
    in.four_pole = p[P_E11] != 0;
    in.f_attack = (float)p[P_E12] / 127.0f;
    in.f_decay = (float)p[P_E13] / 127.0f;
    in.f_sustain = (float)p[P_E14] / 127.0f;
    in.fenv_amt = (float)p[P_E15] / 127.0f;
    in.f_release = rel;
    in.gate = v->gate;
    obxd_render(obxd_slot(t, v), &in, buf, (int)(n < CTL ? n : CTL));
    for (i = 0; i < n && i < CTL; i++)
        out[i] += mulq15(mulq15(buf[i], amp_at(m, i)), VOICE_FS) << 1;
}

/* {OSC1 OSC2 PIT2 DET MIX NOIS PW XMOD} E2(CUT RES MODE POLE F.ATK F.DEC F.SUS F.AMT), {A D S R} */
#define OX(o1, o2, p2, dt, mx, nz, pw, xm) {o1, o2, p2, dt, mx, nz, pw, xm}
static const preset_t OBXD_PRESETS[] = {
    {"OB BRASS", OX(0, 0, 0, 30, 64, 0, 0, 0), {12, 70, 110, 36}, 0, 0, FX(0, 20, 12, 30), XP(0), E2(40, 20, 0, 0, 20, 60, 50, 60)},
    {"JUMP POLY", OX(0, 0, 0, 24, 64, 0, 0, 0), {2, 80, 110, 30}, 0, 0, FX(0, 24, 14, 26), XP(0), E2(70, 16, 0, 1, 0, 50, 70, 30)},
    {"OB STRINGS", OX(0, 0, 0, 40, 64, 0, 0, 0), {60, 90, 118, 80}, 0, 0, FX(0, 0, 16, 60), XP(P_MTYPE + 1, 1), E2(58, 10, 0, 0, 60, 90, 90, 20)},
    {"SYNC LEAD", OX(0, 3, 14, 10, 100, 0, 0, 0), {0, 70, 110, 30}, 0, 1, FX(0, 10, 30, 20), XP(P_GLIDE + 1, 30), E2(80, 24, 0, 1, 0, 60, 60, 30)},
    {"XMOD BELL", OX(1, 1, 19, 10, 64, 0, 40, 60), {0, 100, 40, 70}, 0, 0, FX(0, 10, 30, 40), XP(0), E2(70, 30, 0, 0, 0, 70, 40, 30)},
    {"OB BASS", OX(0, 1, 12, 16, 50, 0, 0, 0), {0, 60, 90, 18}, 0, 1, FX(6, 0, 0, 0), XP(P_TRANS + 1, -24), E2(30, 40, 0, 1, 0, 50, 30, 50)},
    {"FUNK OB BS", OX(1, 1, 0, 20, 64, 0, 30, 0), {0, 56, 60, 16}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -24), E2(26, 60, 0, 1, 0, 40, 20, 60)},
    {"SOFT PAD", OX(1, 1, 0, 30, 64, 4, 60, 0), {70, 90, 118, 90}, 0, 0, FX(0, 0, 20, 64), XP(P_LRATE + 1, 20, P_LD_SHP + 1, 20, P_MTYPE + 1, 3), E2(46, 6, 0, 1, 70, 90, 100, 10)},
    {"BP SWEEP", OX(0, 0, 0, 30, 64, 0, 0, 0), {30, 90, 110, 80}, 0, 0, FX(0, 40, 30, 50), XP(P_LRATE + 1, 10, P_LD_FLT + 1, 30), E2(40, 60, 64, 0, 60, 100, 80, 30)},
    {"HP STAB", OX(0, 0, 7, 20, 64, 0, 0, 0), {0, 70, 50, 30}, 0, 0, FX(0, 20, 30, 30), XP(0), E2(30, 30, 90, 0, 0, 60, 30, 20)},
    {"PULSE KEYS", OX(1, 1, 12, 8, 40, 0, 50, 0), {0, 90, 40, 40}, 0, 0, FX(0, 20, 16, 30), XP(0), E2(60, 10, 0, 1, 0, 70, 30, 40)},
    {"NOISE WIND", OX(0, 0, 0, 0, 0, 127, 0, 0), {60, 90, 118, 80}, 0, 0, FX(0, 20, 20, 60), XP(P_LRATE + 1, 12, P_LD_FLT + 1, 40), E2(50, 90, 40, 0, 40, 90, 80, 10)},
};

static const engine_t ENG_OBXD = {
    "OBXD", {"OSC", "MIX", "FLT", "FENV"},
    {
        {"OSC1", F_ENUM, 0, 2, 0, N_OBXD_OSC1, 0},
        {"OSC2", F_ENUM, 0, 5, 0, N_OBXD_OSC2, 0},
        {"PIT2", F_INT, 0, 48, 0, 0, "st"},
        {"DET", F_PCT, 0, 127, 20, 0, 0},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"PW", F_PCT, 0, 127, 0, 0, 0},
        {"XMOD", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 70, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"MODE", F_PCT, 0, 127, 0, 0, 0},
        {"POLE", F_ENUM, 0, 1, 0, N_OBXD_POLE, 0},
        {"F.ATK", F_INT, 0, 127, 0, 0, 0},
        {"F.DEC", F_INT, 0, 127, 60, 0, 0},
        {"F.SUS", F_PCT, 0, 127, 64, 0, 0},
        {"F.AMT", F_PCT, 0, 127, 30, 0, 0},
    },
    OBXD_PRESETS, sizeof(OBXD_PRESETS) / sizeof(OBXD_PRESETS[0]), 2, obxd_note_on_v, obxd_render_voice,
    0xFDA0u, {P_E8, P_E9, P_E15, P_E3},
    6,                                              /* poly: six Obxd voices a part at most */
};
