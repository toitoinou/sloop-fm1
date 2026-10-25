/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP's audio engine (the firmware's DSP, sequencer and clock) as a plain C library for the mobile app.
 * It wraps firmware/host/preview.c, the adapter the web editor's preview already uses, and adds what an app
 * needs on top: names of engines / presets / kits / drum sounds, whole steps in one call, the fill.
 *
 * Threading: nothing here is thread-safe. Call everything from the audio thread (the app queues the UI's
 * requests and applies them between two se_render() calls). se_render() renders se_block_frames() frames at
 * se_sample_rate(), interleaved stereo float. */
#ifndef SLOOP_ENGINE_H
#define SLOOP_ENGINE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* SLOOP's tracks: 0..2 the synth parts, 3 the drum track */
enum { SE_TRACKS = 4, SE_SYNTHS = 3, SE_DRUM = 3, SE_LANES = 16, SE_MAX_STEPS = 64, SE_MAX_LOCKS = 24 };
/* a drum hit's or a note's level, as the FM-1 stores it */
enum { SE_LV_NORM = 0, SE_LV_GHOST = 1, SE_LV_SOFT = 2, SE_LV_HARD = 3 };
/* a step's fill condition (the engine's own): always, only during a fill, never during one */
enum { SE_FILL_ANY = 0, SE_FILL_ONLY = 1, SE_FILL_NOT = 2 };
/* the track parameters the app's knobs use (core.h P_*) */
enum { SE_P_LEVEL = 0, SE_P_DEC = 2, SE_P_REV = 36, SE_P_PAN = 39, SE_P_MUTE = 40, SE_P_TFLT = 50 };

void se_init(void);
uint32_t se_sample_rate(void);
uint32_t se_block_frames(void);
const float *se_render(void);                 /* se_block_frames() stereo frames, interleaved */

/* transport */
void se_play(int on);
int se_playing(void);
void se_set_bpm(int bpm);
int se_position(int track);                   /* the step playing, -1 when stopped */
void se_set_fill(int on);                     /* held FILL: steps SE_FILL_ONLY play, SE_FILL_NOT do not */
void se_set_swing(int amount);                /* 0 (straight) .. 100 (the most: MPC's 75 %), the 1/16 steps */
void se_set_metronome(int on);                /* a click on every beat while playing, louder on the bar's first */
void se_click(int accent);                    /* one click now (the app's count-in) */
void se_panic(void);

/* sounds. The names and counts read constant tables only: they may be called from any thread */
int se_engine_count(void);
const char *se_engine_name(int engine);
int se_preset_count(int engine);
const char *se_preset_name(int engine, int preset);
void se_set_sound(int track, int engine, int preset);   /* synth tracks: the engine and the whole preset */
int se_sound_engine(int track);
int se_sound_preset(int track);
int se_kit_count(void);                       /* (USR1..USR3+4 play the user sample slots: empty in the app) */
const char *se_kit_name(int kit);
void se_set_kit(int kit);
const char *se_lane_name(int lane);
int se_lane_note(int lane);                   /* the MIDI note (General MIDI drums) a drum lane answers to */

/* what the parameters are (constant tables: any thread). id: core.h P_* (track parameters below SE_P_E0, the
 * engine's own EDIT parameters from SE_P_E0 to SE_P_E0 + 7). Returns 0 for an unknown one */
enum { SE_P_ATK = 1, SE_P_SUS = 3, SE_P_REL = 4, SE_P_DIST = 33, SE_P_CHOR = 34, SE_P_DLY = 35, SE_P_E0 = 63 };
int se_param_count(void);
int se_describe(int engine, int id, int *min, int *max, int *def, const char **label);
const char *se_value_name(int engine, int id, int value);   /* a named value (a wave, a mode), else 0 */
const char *se_engine_page(int engine, int page);           /* the titles of its two EDIT pages ("OSC", "FLT") */
int se_engine_macro(int engine, int k);                     /* its four main parameters (the FM-1's HOME knobs) */
void se_params(int track, int16_t *out);                    /* every parameter of a track (audio thread) */

/* parameters (core.h P_*): range, default, label; set */
void se_param_info(int track, int id, int *min, int *max, int *def, const char **label);
int se_param(int track, int id);
void se_set_param(int track, int id, int value);

/* the pattern of a track */
void se_set_length(int track, int steps);
void se_clear(int track);                     /* empty steps, no lock, no nudge, no condition */
/* a drum step: lanes_on bit k = lane k; level[k], ratchet[k] (1..4) per lane; nudge in 1/64 of a step */
void se_drum_step(int step, uint16_t lanes_on, const uint8_t level[SE_LANES], const uint8_t ratchet[SE_LANES],
                  int nudge, int fill);
/* a synth step: n notes (0 = a rest), MIDI velocity, ratchet 1..4 */
void se_synth_step(int track, int step, const uint8_t *notes, int n, int velocity, int ratchet, int nudge, int fill);
/* the same, where bit k of `cont` says note k goes on from the step before (held, not played again): long notes,
 * one note at a time (SLOOP 2.7's SF_CONT; the notes not listed end here) */
void se_synth_step_cont(int track, int step, const uint8_t *notes, int n, unsigned cont, int velocity, int ratchet,
                        int nudge, int fill);
int se_step_flags(int track, int step);       /* a synth step's flags as the engine has them (tests) */
typedef struct { uint8_t step, param; int16_t value; } se_lock_t;
void se_set_locks(int track, const se_lock_t *locks, int count);   /* replaces every lock of the track */
/* replaces the locks of one step only (their .step is ignored), the locks in force left alone: the app writes
 * the steps just ahead of the playhead, and clears the ones behind it, while the track plays */
void se_set_step_locks(int track, int step, const se_lock_t *locks, int count);

/* playing live */
void se_drum_hit(int lane, int velocity);
void se_note(int track, int note, int on);

#ifdef __cplusplus
}
#endif
#endif
