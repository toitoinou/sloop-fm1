/* SPDX-License-Identifier: GPL-3.0-only */
/* PLAITS (the app's own, FELUCCA_PLAITS): Mutable Instruments' Plaits (Emilie Gillet, MIT), its 24 models
 * as one SLOOP engine. The DSP is Plaits itself (third_party/plaits, through plaits_glue.cc); this file is
 * the engine around it, as the others: EDIT 1 MODEL HARM TIMB MORPH, EDIT 2 DECAY LPG F.ENV T.ENV.
 *
 *   - a SLOOP voice is a Plaits voice (its own slot: part x voice), the note its pitch (glide, LFO, bend, unison
 *     detune included), the key held its trigger and gate;
 *   - SLOOP's envelope and velocity shape the level, as every engine (LPG 0); LPG 1..127 lets Plaits' low-pass
 *     gate ring on its own (DECAY), its value the gate's colour (from a VCA to a low-pass filter);
 *   - the track's modulations go where a filter would: ENV > FLT and LFO > FLT move TIMBRE, LFO > SHP MORPH;
 *   - F.ENV and T.ENV are Plaits' own envelope (DECAY) on the pitch and on TIMBRE (its FM and TIMBRE inputs). */
#include "plaits_glue.h"

static const char *const N_PLAITS_MODEL[] = {"VA VCF", "PH DIST", "FM6 A", "FM6 B", "FM6 C", "TERRAIN", "STR MCH",
                                             "CHIP", "VA", "WAVSHP", "FM2", "GRAIN", "ADDITIV", "WTABLE", "CHORD",
                                             "SPEECH", "SWARM", "NOISE", "PARTCL", "STRING", "MODAL", "BASS DR",
                                             "SNARE", "HI HAT"};

_Static_assert(PLAITS_PARTS == NPART && PLAITS_VOICES == NVOICE, "plaits_glue.h: a slot for every voice");

/* each model's level next to the others' (Q8): the struck and plucked ones ring down, the drums are short */
static const uint16_t PLAITS_GAIN[24] = {256, 256, 256, 256, 256, 256, 256, 230, 256, 256, 256, 256,
                                         256, 256, 380, 300, 256, 256, 420, 620, 420, 420, 480, 520};

static int plaits_slot(const track_t *t, const voice_t *v)
{
    return (int)(t - trk) * PLAITS_VOICES + (int)(v - t->v);
}

static void plaits_note_on(track_t *t, voice_t *v)
{
    plaits_retrigger(plaits_slot(t, v));             /* (a new note on a voice still held: an edge all the same) */
}

static float plaits_unit(int32_t v) { return (float)clamp(v, 0, 127 << 8) / (float)(127 << 8); }

static void plaits_render_voice(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int16_t buf[CTL];
    uint32_t i;
    plaits_in_t in;
    /* the pitch: 1/16 semitones, and fine (a factor 1 + fine / 4096 on the frequency: 0.00423 semitone each) */
    in.note = (float)m->pitch16 / 16.0f + (float)m->fine * 0.0042272f;
    in.harmonics = plaits_unit(p[P_E1] << 8);
    in.timbre = plaits_unit((p[P_E2] << 8) + m->cutoff);
    in.morph = plaits_unit((p[P_E3] << 8) + m->shape - (64 << 8));
    in.decay = (float)p[P_E4] / 127.0f;
    in.lpg = p[P_E5] > 0;
    in.lpg_colour = (float)p[P_E5] / 127.0f;
    in.fm_amount = (float)p[P_E6] / 64.0f;
    in.timbre_amount = (float)p[P_E7] / 64.0f;
    in.model = clamp(p[P_E0], 0, 23);
    in.gate = v->gate;
    plaits_render(plaits_slot(t, v), &in, buf, (int)(n < CTL ? n : CTL));
    {
        const int32_t g = PLAITS_GAIN[in.model];
        for (i = 0; i < n && i < CTL; i++)
            out[i] += mulq15(mulq15(clamp((buf[i] * g) >> 8, -32767, 32767), amp_at(m, i)), VOICE_FS) << 1;
    }
}

static const preset_t PLAITS_PRESETS[] = {
    /* {MODEL, HARM, TIMB, MORPH, DECAY, LPG, F.ENV, T.ENV}, {A D S R}, ENV > FLT (on TIMBRE), mono */
    /* the analog side: Plaits' VA (two waveforms, sync and PWM), its VA through a filter, its wavefolder */
    {"VA LEAD", {8, 40, 70, 30, 64, 0, 0, 0}, {2, 70, 110, 30}, 24, 1, FX(0, 12, 26, 20), XP(P_GLIDE + 1, 36)},
    {"VCF BASS", {0, 24, 36, 40, 64, 0, 0, 0}, {0, 62, 70, 20}, 44, 1, FX(8, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"SYNC BRASS", {8, 90, 40, 70, 64, 0, 0, 0}, {14, 70, 100, 36}, 30, 0, FX(0, 16, 14, 26)},
    {"FOLD BASS", {9, 30, 30, 20, 64, 0, 0, 0}, {0, 60, 80, 20}, 40, 1, FX(10, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"WEST PLUCK", {8, 20, 60, 50, 44, 90, 0, 0}, {0, 90, 127, 50}, 0, 0, FX(0, 10, 34, 30)},
    /* keys and FM */
    {"PD KEYS", {1, 50, 56, 40, 64, 0, 0, 0}, {0, 84, 50, 44}, 30, 0, FX(0, 20, 18, 30)},
    {"FM2 BELL", {10, 70, 46, 60, 64, 0, 0, 0}, {0, 100, 30, 70}, 20, 0, FX(0, 10, 26, 44)},
    {"FM2 BASS", {10, 40, 52, 30, 64, 0, 0, 0}, {0, 56, 60, 20}, 46, 1, FX(6, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"DX BANK A", {2, 0, 64, 64, 64, 0, 0, 0}, {0, 64, 127, 50}, 0, 0, FX(0, 14, 16, 26)},
    {"DX BANK B", {3, 0, 64, 64, 64, 0, 0, 0}, {0, 64, 127, 50}, 0, 0, FX(0, 14, 16, 26)},
    {"ORGAN", {12, 76, 40, 50, 64, 0, 0, 0}, {2, 60, 120, 20}, 0, 0, FX(0, 30, 0, 24)},
    /* pads and textures */
    {"STRINGS", {6, 40, 50, 64, 64, 0, 0, 0}, {60, 90, 118, 84}, 0, 0, FX(0, 50, 18, 60)},
    {"WAVETABLE", {13, 50, 50, 30, 64, 0, 0, 0}, {30, 90, 110, 70}, 26, 0, FX(0, 40, 20, 50), XP(P_LRATE + 1, 20, P_LD_SHP + 1, 24)},
    {"TERRAIN", {5, 64, 40, 64, 64, 0, 0, 0}, {40, 90, 110, 80}, 0, 0, FX(0, 40, 24, 56), XP(P_LRATE + 1, 14, P_LD_SHP + 1, 30)},
    {"SWARM PAD", {16, 50, 70, 50, 64, 0, 0, 0}, {70, 90, 120, 90}, 0, 0, FX(0, 30, 20, 64)},
    {"GRAINS", {11, 50, 60, 70, 64, 0, 0, 0}, {40, 90, 110, 80}, 0, 0, FX(0, 30, 30, 60)},
    {"CHORD STAB", {14, 30, 50, 70, 64, 0, 0, 0}, {0, 70, 60, 40}, 30, 1, FX(0, 20, 30, 40)},
    {"SPEECH", {15, 40, 60, 50, 64, 0, 0, 0}, {0, 70, 120, 30}, 0, 1, FX(0, 10, 20, 30)},
    {"CHIP LEAD", {7, 64, 50, 64, 64, 0, 0, 0}, {0, 60, 110, 20}, 0, 1, FX(0, 0, 26, 16)},
    /* physical models (their own envelope: SLOOP's stays open) */
    {"STRING", {19, 40, 60, 50, 70, 0, 0, 0}, {0, 110, 127, 60}, 0, 0, FX(0, 10, 20, 36)},
    {"BELL", {20, 70, 50, 60, 84, 0, 0, 0}, {0, 110, 127, 70}, 0, 0, FX(0, 10, 26, 50)},
    {"MALLET", {20, 30, 30, 40, 50, 0, 0, 0}, {0, 100, 127, 50}, 0, 0, FX(0, 0, 14, 30)},
    {"PARTICLES", {18, 40, 50, 60, 70, 70, 0, 0}, {0, 100, 127, 70}, 0, 0, FX(0, 20, 30, 60)},
    {"NOISE SWEEP", {17, 50, 40, 60, 64, 0, 0, 0}, {20, 90, 110, 60}, 50, 0, FX(0, 20, 24, 50)},
    /* drums from the keyboard */
    {"PL KICK", {21, 40, 50, 50, 60, 0, 0, 0}, {0, 110, 127, 50}, 0, 1, FX(0, 0, 0, 0), XP(P_TRANS + 1, -24)},
    {"PL SNARE", {22, 50, 50, 50, 50, 0, 0, 0}, {0, 110, 127, 50}, 0, 1, FX(0, 0, 0, 10)},
    {"PL HAT", {23, 50, 60, 40, 30, 0, 0, 0}, {0, 110, 127, 40}, 0, 1, FX(0, 0, 0, 6)},
};

static const engine_t ENG_PLAITS = {
    "PLAITS", {"MODL", "ENV"},
    {
        {"MODEL", F_ENUM, 0, 23, 8, N_PLAITS_MODEL, 0},
        {"HARM", F_PCT, 0, 127, 64, 0, 0},
        {"TIMB", F_PCT, 0, 127, 64, 0, 0},
        {"MORPH", F_PCT, 0, 127, 64, 0, 0},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"LPG", F_PCT, 0, 127, 0, 0, 0},
        {"F.ENV", F_BIPCT, -64, 63, 0, 0, 0},
        {"T.ENV", F_BIPCT, -64, 63, 0, 0, 0},
    },
    PLAITS_PRESETS, sizeof(PLAITS_PRESETS) / sizeof(PLAITS_PRESETS[0]), -1, plaits_note_on, plaits_render_voice,
    0x6E7Fu, {P_E1, P_E2, P_E3, P_E0},
    4,                                              /* poly: four Plaits voices a part at most (the CPU) */
};
