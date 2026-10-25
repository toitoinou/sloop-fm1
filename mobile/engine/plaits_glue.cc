// SPDX-License-Identifier: GPL-3.0-only
// The PLAITS engine's DSP: Mutable Instruments' Plaits (Emilie Gillet, MIT: mobile/third_party/plaits), one
// plaits::Voice per SLOOP voice (3 synth parts x 8 voices), behind a small C API for eng_plaits.c.
#include <cstdio>
#include <cstring>
#include "plaits/dsp/voice.h"
#include "plaits_glue.h"

namespace
{
constexpr int kSlots = PLAITS_PARTS * PLAITS_VOICES;
constexpr size_t kBuffer = 16384;                   // the shared buffer of a Plaits voice (plaits.cc)

struct Slot
{
    plaits::Voice voice;
    char buffer[kBuffer];
    bool ready = false;
    bool low = false;                               // a retrigger: one block with the trigger low first
};
Slot* slots()
{
    static Slot* s = new Slot[kSlots];              // (about 27 kB each, once)
    return s;
}
} // namespace

extern "C" void plaits_init(void)
{
    auto* s = slots();
    for (int i = 0; i < kSlots; ++i)
        if (! s[i].ready)
        {
            stmlib::BufferAllocator allocator (s[i].buffer, kBuffer);
            s[i].voice.Init (&allocator);
            s[i].ready = true;
        }
}

extern "C" void plaits_retrigger(int slot)
{
    if (slot >= 0 && slot < kSlots) slots()[slot].low = true;
}

extern "C" void plaits_render(int slot, const plaits_in_t* in, int16_t* out, int n)
{
    if (slot < 0 || slot >= kSlots) { std::memset (out, 0, sizeof (int16_t) * (size_t) n); return; }
    auto& s = slots()[slot];
    if (! s.ready) plaits_init();
    plaits::Patch patch {};
    patch.note = in->note;
    patch.harmonics = in->harmonics;
    patch.timbre = in->timbre;
    patch.morph = in->morph;
    patch.frequency_modulation_amount = in->fm_amount;
    patch.timbre_modulation_amount = in->timbre_amount;
    patch.morph_modulation_amount = 0.0f;
    patch.engine = in->model;
    patch.decay = in->decay;
    patch.lpg_colour = in->lpg_colour;
    plaits::Modulations mod {};
    mod.engine = 0.0f;
    mod.trigger_patched = true;                     // a note is a trigger (and a gate while held)
    mod.level_patched = ! in->lpg;                  // the LPG off: always open, SLOOP's envelope shapes the note
    mod.level = 1.0f;
    plaits::Voice::Frame frames[plaits::kMaxBlockSize];
    int done = 0;
    while (done < n)
    {
        const int k = std::min (n - done, 16);
        mod.trigger = s.low || ! in->gate ? 0.0f : 1.0f;
        s.low = false;
        s.voice.Render (patch, mod, frames, (size_t) k);
        for (int i = 0; i < k; ++i) out[done + i] = frames[i].out;
        done += k;
    }
}
