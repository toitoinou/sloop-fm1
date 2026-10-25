// SPDX-License-Identifier: GPL-3.0-only
// The project sheet: save the session under a name, open or delete a saved one (a second tap confirms), start a
// new one, export it as a .sloop file or import one (the system's file picker: on Android the Storage Access
// Framework, so Drive, Downloads or a USB stick work too).
#pragma once
#include "Controller.h"
#include "Theme.h"

namespace sloop
{
class ProjectSheet : public juce::Component
{
public:
    ProjectSheet (Controller& c, std::function<void()> close, std::function<void()> settings);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void rebuildList();
    void openRow (int i);
    void deleteRow (int i);
    void save();
    void exportFile();
    void importFile();
    void exportDaw();
    void say (const juce::String& message) { status = message; repaint(); }
    juce::Rectangle<int> panel() const;

    Controller& ctl;
    std::function<void()> onClose, onSettings;
    juce::TextEditor name;
    juce::TextButton saveButton { "Enregistrer" }, newButton { "Nouveau" },
        exportButton { juce::String::fromUTF8 ("Exporter…") }, importButton { juce::String::fromUTF8 ("Importer…") },
        dawButton { juce::String::fromUTF8 ("DAW…") }, settingsButton { juce::String::fromUTF8 ("Réglages") }, ok { "OK" };
    juce::Component listContent;
    juce::Viewport list;
    juce::OwnedArray<juce::TextButton> rows, deletes;
    juce::Array<juce::File> files;
    int armedOpen = -1, armedDelete = -1, armedNew = 0;   // a second tap confirms
    juce::String status;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace sloop
