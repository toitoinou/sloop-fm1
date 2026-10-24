/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP's engine for the mobile app: firmware/host/preview.c (the firmware's DSP and clock, hardware replaced by
 * RAM) in one translation unit, and the calls of sloop_engine.h on top of it. The generated tables
 * (felucca_tables.h and the others) come from mobile/tools/gen_engine_tables.sh. */
#include "../../firmware/host/preview.c"
#include "sloop_engine.h"

_Static_assert(SE_P_LEVEL == P_LEVEL && SE_P_DEC == P_DEC && SE_P_REV == P_REV && SE_P_PAN == P_PAN &&
               SE_P_MUTE == P_MUTE && SE_P_TFLT == P_TFLT, "sloop_engine.h's parameter ids follow core.h");
_Static_assert(SE_P_ATK == P_ATK && SE_P_SUS == P_SUS && SE_P_REL == P_REL && SE_P_DIST == P_DIST &&
               SE_P_CHOR == P_CHOR && SE_P_DLY == P_DLY && SE_P_E0 == P_E0, "sloop_engine.h's parameter ids follow core.h");
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
/* what loading a sound leaves alone: the mix, the pattern and the key (as ui.c param_kept) */
static int se_param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_SLEN && i <= P_SGATE) ||
           (i >= P_ROOT && i <= P_QUANT) || i == P_CHORD || i == P_TFLT || i == P_STRUM || i == P_VLEAD;
}
/* the engine, then the whole preset, as the FM-1 loads one (ui.c set_engine_of, apply_preset_to; ui.c is not in
 * this translation unit): the old sound fades out, what sounds is released, the arpeggiator comes with the preset */
void se_set_sound(int track, int engine, int preset)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    track_t *t;
    const engine_t *e;
    const preset_t *pr;
    uint32_t i;
    if (track < 0 || track >= NPART || preset < 0 || preset >= se_preset_count(engine)) return;
    t = &trk[track];
    e = ENGINES[engine];
    pr = &e->presets[preset];
    locks_restore(t);
    sound_change(t);
    if (t->eng_req != engine) {
        t->eng_req = (uint8_t)engine;
        for (i = 0; i < 8u; i++) t->p[P_E0 + i] = e->edit[i].def;
    }
    panic_req |= (uint8_t)(1u << track);
    t->user = 0;
    t->preset = (uint8_t)preset;
    for (i = 0; i < P_E0; i++)
        if (!se_param_kept(i)) t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++) t->p[P_E0 + i] = (int16_t)pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_ED_FX] = preset_trim((uint32_t)engine, (uint32_t)preset);
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++) {
        t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
        t->p[P_AMODE + i] = (int16_t)(pr->arp[i] ? pr->arp[i] - 1 : TP[P_AMODE + i].def);
    }
    preset_extras(t->p, pr);
    fm6_track_loaded(t);
}
int se_sound_engine(int track) { return track >= 0 && track < NPART ? trk[track].eng_req : -1; }
int se_sound_preset(int track) { return track >= 0 && track < NPART ? trk[track].preset : -1; }
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

int se_param_count(void) { return P_COUNT; }
static const param_desc_t *se_desc(int engine, int id)
{
    if (id < 0 || id >= P_COUNT) return 0;
    if (id < P_E0) return &TP[id];
    if (engine < 0 || engine >= NENGINES) return 0;
    return &ENGINES[engine]->edit[id - P_E0];
}
int se_describe(int engine, int id, int *min, int *max, int *def, const char **label)
{
    const param_desc_t *d = se_desc(engine, id);
    if (!d || !d->label) return 0;
    if (min) *min = d->min;
    if (max) *max = d->max;
    if (def) *def = d->def;
    if (label) *label = d->label;
    return 1;
}
const char *se_value_name(int engine, int id, int value)
{
    const param_desc_t *d = se_desc(engine, id);
    if (!d || d->fmt != F_ENUM || !d->names || value < d->min || value > d->max) return 0;
    return d->names[value - d->min];
}
const char *se_engine_page(int engine, int page)
{
    return engine >= 0 && engine < NENGINES && page >= 0 && page < 2 ? ENGINES[engine]->page_title[page] : "";
}
int se_engine_macro(int engine, int k)
{
    return engine >= 0 && engine < NENGINES && k >= 0 && k < 4 ? ENGINES[engine]->macro[k] : -1;
}
void se_params(int track, int16_t *out)
{
    if (track >= 0 && track < NTRK) memcpy(out, trk[track].p, sizeof trk[track].p);
}

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
void se_synth_step_cont(int track, int step, const uint8_t *notes, int n, unsigned cont, int velocity, int ratchet,
                        int nudge, int fill)
{
    se_synth_step(track, step, notes, n, velocity, ratchet, nudge, fill);
    /* (preview_step keeps only ACCENT and SLIDE: the continuations go in here) */
    if (track >= 0 && track < NPART && step >= 0 && step < NSTEP && n > 0)
        trk[track].step[step].flags |= (uint8_t)((cont & ((1u << clamp(n, 0, 4)) - 1u)) * SF_CONT0);
}
int se_step_flags(int track, int step)
{
    return track >= 0 && track < NPART && step >= 0 && step < NSTEP ? trk[track].step[step].flags : 0;
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
