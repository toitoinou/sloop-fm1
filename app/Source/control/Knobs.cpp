// SPDX-License-Identifier: GPL-3.0-only
// The knobs: the pages of the selected track, its four parameters on each, the held steps' locks.
#include "control/Controller.h"

namespace sloop
{
std::vector<Page> Controller::pages() const
{
    if (isDrum (sel)) return { Page::mix, Page::fx };
    std::vector<Page> p { Page::mix, Page::macro, Page::edit1, Page::edit2 };
    const int e = sounds[(size_t) sel].engine;           // (pages 3 and 4: the engines that have them)
    if (*se_engine_page (e, 2)) p.push_back (Page::edit3);
    if (*se_engine_page (e, 3)) p.push_back (Page::edit4);
    p.push_back (Page::env);
    p.push_back (Page::mod);
    p.push_back (Page::fx);
    return p;
}

juce::String Controller::pageName (Page p) const
{
    const int e = sounds[(size_t) sel].engine;
    switch (p)
    {
        case Page::mix: return "Mix";
        case Page::macro: return "Mac";
        case Page::edit1: return se_engine_page (e, 0);
        case Page::edit2: return se_engine_page (e, 1);
        case Page::edit3: return se_engine_page (e, 2);
        case Page::edit4: return se_engine_page (e, 3);
        case Page::env: return "Env";
        case Page::mod: return "Mod";
        case Page::fx: return "FX";
    }
    return {};
}

std::array<KnobDef, kKnobs> Controller::knobDefs() const
{
    static const int mix[] = { 0, 50, 36, 39 }, env[] = { 1, 2, 3, 4 }, fx[] = { 33, 34, 35, 36 },
                     mod[] = { SE_P_MTYPE, SE_P_MRATE, SE_P_MDEPTH, SE_P_MRISE };
    static const char* mixNames[] = { "Level", "Filtre", "Reverb", "Pan" };
    // the drum track's level and sends are global settings in SLOOP (its pan: each sound's, in the kit)
    static const int drumMix[] = { kGlobal + SE_G_DRLVL, 50, kGlobal + SE_G_DRREV, kGlobal + SE_G_DRDLY };
    static const char* drumMixNames[] = { "Level", "Filtre", "Reverb", "Delay" };
    const int e = isDrum (sel) ? -1 : sounds[(size_t) sel].engine;
    std::array<KnobDef, kKnobs> d;
    for (int k = 0; k < kKnobs; ++k)
    {
        int id = -1;
        switch (page[(size_t) sel])
        {
            case Page::mix: id = isDrum (sel) ? drumMix[k] : mix[k]; break;
            case Page::macro: id = se_engine_macro (e, k); break;
            case Page::edit1: id = SE_P_E0 + k; break;
            case Page::edit2: id = SE_P_E0 + 4 + k; break;
            case Page::edit3: id = SE_P_E0 + 8 + k; break;
            case Page::edit4: id = SE_P_E0 + 12 + k; break;
            case Page::env: id = env[k]; break;
            case Page::mod: id = mod[k]; break;
            case Page::fx: id = fx[k]; break;
        }
        const char* label = nullptr;
        auto& x = d[(size_t) k];
        const bool known = id >= kGlobal ? se_global_describe (id - kGlobal, &x.min, &x.max, &x.def, &label) != 0
                                         : id >= 0 && se_describe (e, id, &x.min, &x.max, &x.def, &label) != 0;
        if (known && label != nullptr && juce::String (label) != "-" && x.max > x.min)
        {
            x.param = id;
            x.valid = true;
            x.label = page[(size_t) sel] == Page::mix ? juce::String (isDrum (sel) ? drumMixNames[k] : mixNames[k]) : juce::String (label);
        }
    }
    return d;
}

juce::String Controller::format (const KnobDef& d, int v) const
{
    if (d.param >= kGlobal) return d.param == kGlobal + SE_G_FILT ? (v == 0 ? juce::String ("off") : (v < 0 ? "LP " : "HP ") + juce::String (std::abs (v)))
                                                                : juce::String (v);
    if (const char* n = se_value_name (isDrum (sel) ? -1 : sounds[(size_t) sel].engine, d.param, v)) return n;
    if (d.param == SE_P_TFLT) return v == 0 ? juce::String ("off") : (v < 0 ? "LP " : "HP ") + juce::String (std::abs (v));
    if (d.param == SE_P_PAN) return v == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (std::abs (v));
    return juce::String (v);
}

int Controller::knob (int k)
{
    const auto d = knobDefs()[(size_t) k];
    if (! d.valid) return 0;
    if (d.param >= kGlobal) return globals[(size_t) (d.param - kGlobal)];
    if (! held.empty())
        if (auto* c = editClip())
            if (const auto v = c->data.lock (held.front().second, d.param); v != kNoLock) return v;
    return params[(size_t) sel][(size_t) d.param];
}

bool Controller::knobLocked (int k)
{
    const auto d = knobDefs()[(size_t) k];
    auto* c = editClip();
    return d.valid && d.param < kGlobal && ! held.empty() && c != nullptr && c->data.lock (held.front().second, d.param) != kNoLock;
}

void Controller::setKnob (int k, int v)
{
    const auto d = knobDefs()[(size_t) k];
    if (! d.valid) return;
    v = juce::jlimit (d.min, d.max, v);
    if (d.param >= kGlobal) { setGlobal (d.param - kGlobal, v); return; }   // (a global setting: no lock)
    if (! held.empty())
    {
        if (auto* c = editClip())
        {
            remember ("lock");
            for (auto& [row, s] : held) c->data.setLock (s, d.param, v);
            lockedSinceHold = true;
            stepChanged();
        }
        return;
    }
    params[(size_t) sel][(size_t) d.param] = v;
    audio.setParam (sel, d.param, v);
    recordMotion (d.param, v);
    notify();
}

void Controller::resetKnob (int k)
{
    const auto d = knobDefs()[(size_t) k];
    if (! d.valid) return;
    if (! held.empty())
    {
        remember ("lock");
        if (auto* c = editClip())
            for (auto& [row, s] : held) c->data.clearLock (s, d.param);
        lockedSinceHold = true;
        stepChanged();
        return;
    }
    setKnob (k, d.def);
}
} // namespace sloop
