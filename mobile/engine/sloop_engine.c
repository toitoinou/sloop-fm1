/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP's engine for the mobile app: firmware/host/preview.c (the firmware's DSP and clock, hardware replaced by
 * RAM) in one translation unit, and the calls of sloop_engine.h on top of it. The generated tables
 * (felucca_tables.h and the others) come from mobile/tools/gen_engine_tables.sh. */
#include "../../firmware/host/preview.c"
#include "sloop_engine.h"

_Static_assert(SE_P_LEVEL == P_LEVEL && SE_P_DEC == P_DEC && SE_P_REV == P_REV && SE_P_PAN == P_PAN &&
               SE_P_MUTE == P_MUTE && SE_P_TFLT == P_TFLT, "sloop_engine.h's parameter ids follow core.h");
_Static_assert(SE_DRUM == TRK_DRUM && SE_TRACKS == NTRK && SE_LANES == DRUM_LANES && SE_MAX_STEPS == NSTEP &&
               SE_MAX_LOCKS == NLOCK, "sloop_engine.h's sizes follow core.h");

void se_init(void)
{
    preview_init();
    preview_render();               /* as the web preview: the first block sets the engine's buffers up */
}
uint32_t se_sample_rate(void) { return FS; }
uint32_t se_block_frames(void) { return CTL; }
const float *se_render(void) { return preview_render(); }

void se_play(int on) { preview_play(on != 0); }
int se_playing(void) { return song.playing; }
void se_set_bpm(int bpm) { preview_global(G_BPM, bpm); }
int se_position(int track) { return preview_position((uint32_t)track); }
void se_set_fill(int on) { fill_held = (uint8_t)(on != 0); }
void se_panic(void) { preview_panic(); }

int se_engine_count(void) { return NENGINES; }
const char *se_engine_name(int engine)
{
    return engine >= 0 && engine < NENGINES ? ENGINES[engine]->name : "";
}
int se_preset_count(int engine)
{
    return engine >= 0 && engine < NENGINES ? ENGINES[engine]->npresets : 0;
}
const char *se_preset_name(int engine, int preset)
{
    if (preset < 0 || preset >= se_preset_count(engine)) return "";
    return ENGINES[engine]->presets[preset].name;
}
void se_set_sound(int track, int engine, int preset)
{
    if (track < 0 || track >= NPART || preset < 0 || preset >= se_preset_count(engine)) return;
    preview_track((uint32_t)track, (uint32_t)engine, (uint32_t)preset);
}
int se_kit_count(void) { return (int)(DRUM_KITS < NELEM(DRUM_KIT_NAMES) ? DRUM_KITS : NELEM(DRUM_KIT_NAMES)); }
const char *se_kit_name(int kit) { return kit >= 0 && kit < se_kit_count() ? DRUM_KIT_NAMES[kit] : ""; }
void se_set_kit(int kit) { preview_param(TRK_DRUM, P_E0, kit); }
const char *se_lane_name(int lane) { return lane >= 0 && lane < DRUM_LANES ? LANE_NAME[lane] : ""; }

void se_param_info(int track, int id, int *min, int *max, int *def, const char **label)
{
    const param_desc_t *d;
    if (track < 0 || track >= NTRK || id < 0 || id >= P_COUNT) return;
    d = lock_desc(&trk[track], (uint32_t)id);
    if (id < P_E0) d = &TP[id];
    if (min) *min = d->min;
    if (max) *max = d->max;
    if (def) *def = d->def;
    if (label) *label = d->label;
}
int se_param(int track, int id) { return track >= 0 && track < NTRK && id >= 0 && id < P_COUNT ? trk[track].p[id] : 0; }
void se_set_param(int track, int id, int value) { preview_param((uint32_t)track, (uint32_t)id, value); }

void se_set_length(int track, int steps) { preview_param((uint32_t)track, P_SLEN, steps); }
void se_clear(int track)
{
    if (track < 0 || track >= NTRK) return;
    locks_restore(&trk[track]);
    steps_clear(&trk[track]);
}
static void put_step(int track, int step, const void *s, int nudge, int fill)
{
    memcpy(transfer, s, 10);
    transfer[10] = (uint8_t)(int8_t)clamp(nudge, MICRO_MIN, MICRO_MAX);
    transfer[11] = (uint8_t)(fill & 3);
    preview_step((uint32_t)track, (uint32_t)step);
}
void se_drum_step(int step, uint16_t lanes_on, const uint8_t level[SE_LANES], const uint8_t ratchet[SE_LANES],
                  int nudge, int fill)
{
    dstep_t d;
    uint32_t k;
    if (step < 0 || step >= NSTEP) return;
    memset(&d, 0, sizeof d);
    d.on[0] = (uint8_t)lanes_on;
    d.on[1] = (uint8_t)(lanes_on >> 8);
    for (k = 0; k < DRUM_LANES; k++) {
        uint32_t lv = level ? level[k] & 3u : 0, rt = ratchet && ratchet[k] ? (ratchet[k] - 1u) & 3u : 0;
        d.lvl[k / 4] |= (uint8_t)(lv << ((k % 4) * 2));
        d.rat[k / 4] |= (uint8_t)(rt << ((k % 4) * 2));
    }
    put_step(TRK_DRUM, step, &d, nudge, fill);
}
void se_synth_step(int track, int step, const uint8_t *notes, int n, int velocity, int ratchet, int nudge, int fill)
{
    step_t s;
    uint32_t j, rt = ratchet > 1 ? (uint32_t)(ratchet - 1) & 3u : 0;
    if (track < 0 || track >= NPART || step < 0 || step >= NSTEP) return;
    memset(&s, 0, sizeof s);
    s.n = (uint8_t)clamp(n, 0, 4);
    for (j = 0; j < s.n; j++) {
        s.note[j] = notes[j] & 127u;
        s.rat |= (uint8_t)(rt << (j * 2));
    }
    s.time = s.n ? ST_NOTE : ST_REST;
    s.vel = (uint8_t)clamp(velocity, 1, 127);
    put_step(track, step, &s, nudge, fill);
}
void se_set_locks(int track, const se_lock_t *locks, int count)
{
    int j;
    if (track < 0 || track >= NTRK) return;
    for (j = 0; j < count && j < NLOCK; j++) {
        transfer[j * 4] = locks[j].step;
        transfer[j * 4 + 1] = locks[j].param;
        transfer[j * 4 + 2] = (uint8_t)(locks[j].value & 0xFF);
        transfer[j * 4 + 3] = (uint8_t)((uint16_t)locks[j].value >> 8);
    }
    preview_locks((uint32_t)track, (uint32_t)(count < NLOCK ? count : NLOCK));
}

void se_drum_hit(int lane, int velocity)
{
    if (lane >= 0 && lane < DRUM_LANES) drum_on(LANE_NOTE[lane], (uint32_t)clamp(velocity, 1, 127));
}
void se_note(int track, int note, int on) { preview_note((uint32_t)track, (uint32_t)note, (uint32_t)(on != 0)); }
