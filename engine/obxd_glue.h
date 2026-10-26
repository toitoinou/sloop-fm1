/* SPDX-License-Identifier: GPL-3.0-only */
/* The OBXD engine's DSP (obxd_glue.cc: Obxd's voice by Vadim Filatov, third_party/obxd) for eng_obxd.c: one
 * Obxd voice per slot (part * OBXD_VOICES + voice), at 44.1 kHz. Audio thread only. */
#ifndef OBXD_GLUE_H
#define OBXD_GLUE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { OBXD_PARTS = 7, OBXD_VOICES = 8 };
typedef struct {
    float note;                                     /* MIDI note, with its fraction */
    int osc1, osc2;                                 /* 0 SAW 1 PULSE 2 both; osc 2: + 3 hard-synced to osc 1 */
    float osc2_pitch;                               /* semitones above osc 1, 0..48 */
    float detune, mix, noise, pw, xmod;             /* 0..1 */
    float cutoff, reso, multimode, fenv_amt;        /* 0..1; cutoff with the track's modulation, may leave 0..1 */
    int four_pole;
    float f_attack, f_decay, f_sustain, f_release;  /* 0..1 */
    int gate;
} obxd_in_t;
void obxd_init(void);
void obxd_note_on(int slot, int note, int velocity);
void obxd_render(int slot, const obxd_in_t *in, int16_t *out, int n);
#ifdef __cplusplus
}
#endif
#endif
