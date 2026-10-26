// SPDX-License-Identifier: GPL-3.0-only
// The sound browser (the whole screen): the kinds of sound and the engines, their sounds as tiles; and the
// sounds' categories, the engines' descriptions.
#pragma once
#include "control/Controller.h"
#include "ui/Theme.h"
#include <map>

namespace sloop
{
// the sound of the selected track, on the whole screen: on the left the kinds of sound (from every engine) then the
// engines, on the right their sounds; a tap plays one at once (a note, or a few hits for a kit), < > the one before /
// after; the sheet stays open to try the next. DR: its kits
class SoundSheet : public juce::Component
{
public:
    SoundSheet (Controller& c, std::function<void()> close);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Item { int engine, preset; };                 // engine -1: a kit
    struct Row { juce::String label; int kind, index; }; // kind 0 a heading, 1 a kind of sound, 2 an engine
    struct List : juce::Component
    {
        explicit List (SoundSheet& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        SoundSheet& owner;
    };
    struct Grid : juce::Component
    {
        explicit Grid (SoundSheet& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        juce::Rectangle<int> tile (int i) const;
        int cols() const { return juce::jmax (2, getWidth() / 170); }
        SoundSheet& owner;
    };
    void showEngine (int engine);
    void showCategory (int category);
    void pick (int index);
    int current() const;                                 // the item that plays now, -1 none in this list
    Controller& ctl;
    std::function<void()> onClose;
    int engineShown = 0, categoryShown = -1;             // a kind of sound across the engines, or one engine
    std::vector<Row> rows;
    std::vector<Item> items;
    List list { *this };
    Grid grid { *this };
    juce::Viewport listView, gridView;
    juce::TextButton prev { "<" }, next { ">" }, ok { "OK" };
    static constexpr int rowH = 40, tileH = 54;
};

void auditionSound (Controller&);                        // the track's sound, briefly (a note, a few drum hits)

void stepSound (Controller&, int direction);             // the track's next / previous preset (its engine's), or kit

juce::String engineAbout (const juce::String& engineName);   // an engine in a line

int soundCategoryCount();                                     // the kinds of sound (Basses, Leads, Nappes...)

juce::String soundCategoryName (int category);

int soundCategory (const juce::String& presetName);           // a preset's kind, from its name
} // namespace sloop
