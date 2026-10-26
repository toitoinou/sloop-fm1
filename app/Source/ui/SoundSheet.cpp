// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SoundSheet.h"
#include "ui/Common.h"
#include "sloop_engine.h"

namespace sloop
{
using namespace ui;

void auditionSound (Controller& ctl)
{
    auto* a = &ctl.audio;
    if (isDrum (ctl.sel))
    {
        a->drumHit (0, 110);
        juce::Timer::callAfterDelay (220, [a] { a->drumHit (4, 90); });
        juce::Timer::callAfterDelay (440, [a] { a->drumHit (3, 110); });
        return;
    }
    const int t = ctl.sel, note = ctl.note (t, 7);
    a->note (t, note, true);
    juce::Timer::callAfterDelay (450, [a, t, note] { a->note (t, note, false); });
}

void stepSound (Controller& ctl, int direction)
{
    const auto snd = ctl.sounds[(size_t) ctl.sel];
    if (isDrum (ctl.sel))
    {
        int k = snd.preset;
        for (int i = 0; i < se_kit_count(); ++i)          // (the user sample kits are empty in the app: skipped)
        {
            k = (k + direction + se_kit_count()) % se_kit_count();
            if (! juce::String (se_kit_name (k)).startsWith ("USR")) break;
        }
        ctl.setSound (0, -1, k);
    }
    else
    {
        const int n = juce::jmax (1, se_preset_count (snd.engine));
        ctl.setSound (ctl.sel, snd.engine, (snd.preset + direction + n) % n);
    }
    auditionSound (ctl);
}

SoundSheet::SoundSheet (Controller& c, std::function<void()> close) : ctl (c), onClose (std::move (close))
{
    listView.setViewedComponent (&list, false);
    gridView.setViewedComponent (&grid, false);
    for (auto* v : { &listView, &gridView })
    {
        v->setScrollBarsShown (false, false, true, false);
        v->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (v);
    }
    for (auto* b : { &prev, &next, &ok }) addAndMakeVisible (b);
    ok.setToggleState (true, juce::dontSendNotification);
    ok.onClick = [this] { onClose(); };
    prev.onClick = [this] { if (! items.empty()) pick (current() < 0 ? 0 : (current() + (int) items.size() - 1) % (int) items.size()); };
    next.onClick = [this] { if (! items.empty()) pick (current() < 0 ? 0 : (current() + 1) % (int) items.size()); };
    if (isDrum (ctl.sel))
    {
        listView.setVisible (false);
        for (int k = 0; k < se_kit_count(); ++k)
            if (! juce::String (se_kit_name (k)).startsWith ("USR"))   // (the user sample slots: nothing in the app yet)
                items.push_back ({ -1, k });
    }
    else
    {
        rows.push_back ({ "TYPE DE SON", 0, -1 });
        for (int k = 0; k < soundCategoryCount(); ++k) rows.push_back ({ soundCategoryName (k), 1, k });
        rows.push_back ({ "MOTEUR", 0, -1 });
        for (int e = 0; e < se_engine_count(); ++e) rows.push_back ({ se_engine_name (e), 2, e });
        showEngine (ctl.sounds[(size_t) ctl.sel].engine);
    }
}

void SoundSheet::showEngine (int engine)
{
    engineShown = engine;
    categoryShown = -1;
    items.clear();
    for (int p = 0; p < se_preset_count (engine); ++p) items.push_back ({ engine, p });
    resized();
    repaint();
}

void SoundSheet::showCategory (int category)
{
    categoryShown = category;
    items.clear();
    for (int e = 0; e < se_engine_count(); ++e)
        for (int p = 0; p < se_preset_count (e); ++p)
            if (soundCategory (se_preset_name (e, p)) == category) items.push_back ({ e, p });
    resized();
    repaint();
}

int SoundSheet::current() const
{
    const auto snd = ctl.sounds[(size_t) ctl.sel];
    for (size_t i = 0; i < items.size(); ++i)
        if ((isDrum (ctl.sel) || items[i].engine == snd.engine) && items[i].preset == snd.preset) return (int) i;
    return -1;
}

void SoundSheet::pick (int index)
{
    if (index < 0 || index >= (int) items.size()) return;
    const auto it = items[(size_t) index];
    ctl.setSound (ctl.sel, it.engine, it.preset);
    auditionSound (ctl);
    // (the tile in sight)
    const auto r = grid.tile (index);
    if (r.getY() < gridView.getViewPositionY() || r.getBottom() > gridView.getViewPositionY() + gridView.getHeight())
        gridView.setViewPosition (0, juce::jmax (0, r.getY() - tileH));
    repaint();
    grid.repaint();
}

void SoundSheet::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (44);
    ok.setBounds (top.removeFromRight (80)); top.removeFromRight (8);
    next.setBounds (top.removeFromRight (52)); top.removeFromRight (6);
    prev.setBounds (top.removeFromRight (52));
    r.removeFromTop (8);
    if (listView.isVisible())
    {
        listView.setBounds (r.removeFromLeft (juce::jlimit (130, 190, r.getWidth() / 4)));
        r.removeFromLeft (10);
        list.setSize (listView.getWidth(), (int) rows.size() * rowH);
    }
    gridView.setBounds (r);
    const int n = (int) items.size(), cols = juce::jmax (2, r.getWidth() / 170);
    grid.setSize (r.getWidth(), juce::jmax (1, (n + cols - 1) / cols) * tileH);
    if (const int c = current(); c >= 0)              // (the sound playing in sight)
        if (grid.tile (c).getBottom() > gridView.getViewPositionY() + gridView.getHeight())
            gridView.setViewPosition (0, grid.tile (c).getY() - tileH);
}

void SoundSheet::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto title = getLocalBounds().reduced (10, 8).removeFromTop (44).withRight (prev.getX() - 8);
    g.setColour (theme::track (ctl.sel));
    g.fillRect (title.getX(), title.getCentreY() - 9, 3, 18);
    const auto snd = ctl.sounds[(size_t) ctl.sel];
    const juce::String now = isDrum (ctl.sel) ? juce::String (se_kit_name (snd.preset))
                                              : juce::String (se_preset_name (snd.engine, snd.preset)) + juce::String::fromUTF8 ("  ·  ")
                                                    + se_engine_name (snd.engine);
    g.setColour (theme::fg);
    g.setFont (font (18.0f, true));
    g.drawText (juce::String (trackId (ctl.sel)) + juce::String::fromUTF8 ("  ") + now, title.withTrimmedLeft (12),
                juce::Justification::centredLeft, true);
}

void SoundSheet::List::paint (juce::Graphics& g)
{
    for (size_t i = 0; i < owner.rows.size(); ++i)
    {
        const auto& row = owner.rows[i];
        auto r = juce::Rectangle<int> (0, (int) i * rowH, getWidth(), rowH).reduced (0, 2);
        if (row.kind == 0)
        {
            g.setColour (theme::dim);
            g.setFont (font (11.0f, true));
            g.drawText (row.label, r.withTrimmedLeft (6), juce::Justification::bottomLeft);
            continue;
        }
        const bool on = row.kind == 1 ? owner.categoryShown == row.index : owner.categoryShown < 0 && owner.engineShown == row.index;
        const bool playing = row.kind == 2 && owner.ctl.sounds[(size_t) owner.ctl.sel].engine == row.index;
        g.setColour (on ? theme::cellBeat : theme::panel);
        g.fillRoundedRectangle (r.toFloat(), theme::radius);
        if (on) { g.setColour (theme::track (owner.ctl.sel)); g.fillRect (r.removeFromLeft (3)); }
        g.setColour (on ? theme::fg : theme::fg.withAlpha (0.8f));
        g.setFont (font (14.0f, on));
        g.drawText (row.label + (playing ? juce::String::fromUTF8 ("  •") : juce::String()), r.withTrimmedLeft (10),
                    juce::Justification::centredLeft, true);
    }
}

void SoundSheet::List::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown()) return;
    const int i = e.getPosition().y / rowH;
    if (i < 0 || i >= (int) owner.rows.size()) return;
    const auto row = owner.rows[(size_t) i];
    if (row.kind == 1) owner.showCategory (row.index);
    if (row.kind == 2) owner.showEngine (row.index);
    repaint();
}

juce::Rectangle<int> SoundSheet::Grid::tile (int i) const
{
    const int c = cols(), w = getWidth() / c;
    return { (i % c) * w, (i / c) * tileH, w - 6, tileH - 6 };
}

void SoundSheet::Grid::paint (juce::Graphics& g)
{
    const int cur = owner.current();
    const bool across = owner.categoryShown >= 0;
    for (int i = 0; i < (int) owner.items.size(); ++i)
    {
        const auto it = owner.items[(size_t) i];
        auto r = tile (i).toFloat();
        const bool on = i == cur;
        g.setColour (on ? theme::fg : theme::panel);
        g.fillRoundedRectangle (r, theme::radius);
        auto text = r.reduced (10.0f, 6.0f);
        g.setColour (on ? theme::ink : theme::fg);
        g.setFont (font (14.0f, true));
        const juce::String name = it.engine < 0 ? juce::String (se_kit_name (it.preset)) : juce::String (se_preset_name (it.engine, it.preset));
        if (across)
        {
            g.drawText (name, text.removeFromTop (text.getHeight() * 0.58f), juce::Justification::bottomLeft, true);
            g.setColour (on ? theme::ink.withAlpha (0.6f) : theme::dim);
            g.setFont (font (11.0f));
            g.drawText (se_engine_name (it.engine), text, juce::Justification::topLeft, true);
        }
        else
            g.drawText (name, text, juce::Justification::centredLeft, true);
    }
}

void SoundSheet::Grid::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown()) return;
    for (int i = 0; i < (int) owner.items.size(); ++i)
        if (tile (i).contains (e.getPosition())) { owner.pick (i); return; }
}

// the kinds of sound, from the presets' names (across the engines: the sound sheet's first buttons)
namespace
{
const char* const kCategories[] = { "Basses", "Leads", "Nappes", "Claviers", "Cloches", "Plucks", "Stabs", "Percus",
                                    "Textures" };
}

int soundCategoryCount() { return (int) std::size (kCategories); }

juce::String soundCategoryName (int c) { return juce::String::fromUTF8 (kCategories[juce::jlimit (0, soundCategoryCount() - 1, c)]); }

int soundCategory (const juce::String& name)
{
    auto has = [&name] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (name.containsWholeWord (w)) return true;
        return false;
    };
    if (has ({ "KICK", "SNARE", "HAT", "DRUM", "TOMS", "TABLA", "CONGA", "TIMPANI", "BLOCK", "KIT" })) return 7;
    if (has ({ "WIND", "RAIN", "NOISE", "VINYL", "HISS", "RISER", "RADIO", "ARCADE", "OCEAN", "METAL", "BITCRUSH", "NZ",
               "PARTICLES", "SPEECH", "ROBOT", "DRONE", "SWEEP", "CLOUD", "DUST", "HAZE", "SHIMMER", "FREEZE", "TERRAIN",
               "GRAINS", "SCRATCH", "BEEP" }))
        return 8;
    if (has ({ "BASS", "BS", "808", "SUB", "REESE", "WOBBLE", "BOOM", "ACID", "303" })) return 0;
    if (has ({ "PLUCK", "HARP", "GTR", "KOTO", "BANJO", "SITAR", "NYLON", "PICK" })) return 5;
    if (has ({ "PAD", "STR", "STRING", "STRINGS", "CHOIR", "OOH", "AAH", "ATMOS", "ENSEMBLE", "CELLO", "BOW", "BOWED",
               "TANPURA", "SWARM", "WAVETABLE" }))
        return 2;
    if (has ({ "LEAD", "LD", "FLUTE", "HARMONICA", "TALKBOX", "SUPERSAW", "HOOVER" })) return 1;
    if (has ({ "BELL", "BELLS", "MALLET", "MARIMBA", "KALIMBA", "GLOCKEN", "CELESTA", "XYLOPHONE", "MUSIC", "VIBES", "VIBRA",
               "CHIMES", "GLASS", "STEEL", "THUMB", "BAR", "BARS", "MODAL", "TREE" }))
        return 4;
    if (has ({ "BRASS", "STAB", "HORN", "CHORD", "SECT", "RAVE", "POWER" })) return 6;
    return 3;                                        // (what is left plays as keys)
}

// what each engine is, in a line (the sound sheet), by its name
juce::String engineAbout (const juce::String& name)
{
    static const std::pair<const char*, const char*> about[] = {
        { "ANALOG", "Soustractif : deux oscillateurs (chacun sa forme), sous-oscillateur, sync, filtre 24 dB à la Moog et "
                    "son enveloppe. Basses 808, acid, leads, nappes." },
        { "DIGITAL", "FM à 4 opérateurs : pianos électriques, cloches, claviers, basses métalliques." },
        { "PHASE", "Distorsion de phase, à la Casio CZ : des sons numériques ronds ou mordants." },
        { "LOFI", "Puce 8 bits : carré, triangle, bruit, onde 4 bits. Les sons des consoles." },
        { "SAMPLE", "Des instruments enregistrés (piano, contrebasse, cuivres, cordes...) joués au clavier." },
        { "VOICE", "Des voyelles chantées (formants) : chœurs, talkbox, robot." },
        { "TRIO", "Trois oscillateurs, ring mod et sync, un filtre : les puces des ordinateurs 8 bits, les stabs rave." },
        { "WHEEL", "Orgue à roues phoniques : tirettes, percussion, cabine tournante." },
        { "GRAIN", "Granulaire sur les sons enregistrés : nuages, textures, nappes figées." },
        { "FM6", "FM à 6 opérateurs, à la DX7 (le cœur de Dexed) : les classiques des années 80." },
        { "PHYS", "Modèles physiques (Rings) : cordes pincées ou frottées, cloches, percussions." },
        { "NOISE", "Des bruits filtrés : vent, pluie, vinyle, montées, effets." },
        { "PLAITS", "Mutable Instruments Plaits : 24 modèles (analogique, FM, wavetable, accords, voix, cordes, percussions) "
                    "réglés par 4 macros : MODEL, HARM, TIMB, MORPH." },
    };
    for (auto& [n, text] : about)
        if (name == n) return juce::String::fromUTF8 (text);
    return {};
}
} // namespace sloop
