/* SPDX-License-Identifier: GPL-3.0-only */
/* The PLAITS engine's DSP (plaits_glue.cc, Mutable Instruments' Plaits) for eng_plaits.c: one Plaits voice per
 * slot (part * PLAITS_VOICES + voice), rendered at 44.1 kHz. Audio thread only. */
#ifndef PLAITS_GLUE_H
#define PLAITS_GLUE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { PLAITS_PARTS = 3, PLAITS_VOICES = 8 };
typedef struct {
    float note;                                     /* MIDI note, with its fraction */
    float harmonics, timbre, morph;                 /* 0..1 */
    float fm_amount, timbre_amount;                 /* -1..1: the internal envelope on the pitch, on TIMBRE */
    float decay, lpg_colour;                        /* 0..1 */
    int model;                                      /* 0..23 */
    int gate;                                       /* the key is held */
    int lpg;                                        /* the low-pass gate (its own decay) on */
} plaits_in_t;
void plaits_init(void);
void plaits_retrigger(int slot);                    /* a new note on a voice still held: a fresh trigger */
void plaits_render(int slot, const plaits_in_t *in, int16_t *out, int n);
#ifdef __cplusplus
}
#endif
#endif
