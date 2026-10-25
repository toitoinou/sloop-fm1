// SPDX-License-Identifier: GPL-3.0-only
#include "ProjectSheet.h"
#include "DawProject.h"

namespace sloop
{
namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
}

ProjectSheet::ProjectSheet (Controller& c, std::function<void()> close, std::function<void()> settings)
    : ctl (c), onClose (std::move (close)), onSettings (std::move (settings))
{
    name.setText (ctl.projectName, false);
    name.setFont (theme::font (16.0f));
    name.setColour (juce::TextEditor::backgroundColourId, theme::cell);
    name.setColour (juce::TextEditor::textColourId, theme::fg);
    name.setColour (juce::TextEditor::outlineColourId, theme::line);
    name.setIndents (10, 8);
    name.onReturnKey = [this] { save(); };
    list.setViewedComponent (&listContent, false);
    list.setScrollBarsShown (false, false, true, false);
    list.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    for (auto* b : { &saveButton, &newButton, &exportButton, &importButton, &dawButton, &settingsButton, &ok }) addAndMakeVisible (b);
    addAndMakeVisible (name);
    addAndMakeVisible (list);
    saveButton.onClick = [this] { save(); };
    newButton.onClick = [this]
    {
        if (armedNew == 0) { armedNew = 1; newButton.setButtonText (u8 ("Vider ?")); say (u8 ("Touche encore « Vider ? » : la session en cours sera remplacée par une vide (enregistre-la d'abord si tu veux la garder).")); return; }
        ctl.load (Project::empty (u8 ("Sans titre")));
        name.setText (ctl.projectName, false);
        armedNew = 0;
        newButton.setButtonText ("Nouveau");
        say ("Nouveau projet.");
    };
    exportButton.onClick = [this] { exportFile(); };
    importButton.onClick = [this] { importFile(); };
    dawButton.onClick = [this] { exportDaw(); };
    settingsButton.onClick = [this] { onSettings(); };
    ok.onClick = [this] { onClose(); };
    ok.setToggleState (true, juce::dontSendNotification);
    rebuildList();
}

void ProjectSheet::rebuildList()
{
    rows.clear();
    deletes.clear();
    files = store::projects();
    for (int i = 0; i < files.size(); ++i)
    {
        const auto f = files[i];
        const juce::String when = f.getLastModificationTime().formatted ("%d/%m %H:%M");
        auto* row = rows.add (new juce::TextButton (i == armedOpen ? u8 ("Ouvrir « ") + f.getFileNameWithoutExtension() + u8 (" » ?")
                                                                   : f.getFileNameWithoutExtension() + "   " + when));
        row->setToggleState (i == armedOpen, juce::dontSendNotification);
        // (the list is rebuilt after the click: a button must not delete itself inside its own callback)
        row->onClick = [this, i] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ProjectSheet> (this), i] { if (sp) sp->openRow (i); }); };
        auto* del = deletes.add (new juce::TextButton (i == armedDelete ? u8 ("Suppr. ?") : u8 ("✕")));
        del->setToggleState (i == armedDelete, juce::dontSendNotification);
        del->onClick = [this, i] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ProjectSheet> (this), i] { if (sp) sp->deleteRow (i); }); };
        listContent.addAndMakeVisible (row);
        listContent.addAndMakeVisible (del);
    }
    resized();
}

void ProjectSheet::openRow (int i)
{
    if (i < 0 || i >= files.size()) return;
    if (armedOpen != i)
    {
        armedOpen = i;
        armedDelete = -1;
        say (u8 ("Touche encore pour l'ouvrir : la session en cours sera remplacée."));
        rebuildList();
        return;
    }
    if (auto p = store::read (files[i]))
    {
        ctl.load (*p);
        name.setText (ctl.projectName, false);
        say (u8 ("Ouvert : ") + ctl.projectName);
    }
    else say (u8 ("Ce fichier ne se lit pas (abîmé, ou d'une version plus récente de l'appli)."));
    armedOpen = -1;
    rebuildList();
}

void ProjectSheet::deleteRow (int i)
{
    if (i < 0 || i >= files.size()) return;
    if (armedDelete != i) { armedDelete = i; armedOpen = -1; rebuildList(); return; }
    const auto f = files[i];
    f.deleteFile();
    say (u8 ("Supprimé : ") + f.getFileNameWithoutExtension());
    armedDelete = -1;
    rebuildList();
}

void ProjectSheet::save()
{
    auto n = name.getText().trim();
    if (n.isEmpty()) n = u8 ("Sans titre");
    ctl.projectName = n;
    const auto f = store::fileFor (n);
    if (store::write (f, ctl.snapshot())) say (u8 ("Enregistré : ") + f.getFileNameWithoutExtension());
    else say (u8 ("L'enregistrement a échoué (plus de place ?)."));
    armedOpen = armedDelete = -1;
    rebuildList();
    ctl.notify();
}

void ProjectSheet::exportFile()
{
    const auto n = ctl.projectName.isNotEmpty() ? ctl.projectName : u8 ("Sans titre");
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (juce::File::createLegalFileName (n) + ".sloop");
    chooser = std::make_unique<juce::FileChooser> (u8 ("Exporter le projet"), start, "*.sloop");
    const auto json = ctl.snapshot().toJson();
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, json] (const juce::FileChooser& fc)
                          {
                              const auto url = fc.getURLResult();
                              if (url.isEmpty()) return;
                              bool done = false;
                              if (url.isLocalFile())
                                  done = url.getLocalFile().replaceWithText (json);
                              else if (auto out = url.createOutputStream())   // (a content:// document on Android)
                                  done = out->writeText (json, false, false, nullptr);
                              say (done ? u8 ("Exporté.") : u8 ("L'export a échoué."));
                          });
}

// for a DAW: Bitwig Studio, Studio One, Cubase... open it (the scenes in the clip launcher, and in the arrangement)
void ProjectSheet::exportDaw()
{
    const auto n = ctl.projectName.isNotEmpty() ? ctl.projectName : u8 ("Sans titre");
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (juce::File::createLegalFileName (n) + ".dawproject");
    chooser = std::make_unique<juce::FileChooser> (u8 ("Exporter pour un DAW"), start, "*.dawproject");
    const auto data = dawproject::zip (ctl.snapshot());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, data] (const juce::FileChooser& fc)
                          {
                              const auto url = fc.getURLResult();
                              if (url.isEmpty()) return;
                              bool done = false;
                              if (url.isLocalFile())
                                  done = url.getLocalFile().replaceWithData (data.getData(), data.getSize());
                              else if (auto out = url.createOutputStream())
                                  done = out->write (data.getData(), data.getSize());
                              say (done ? u8 ("Exporté en DAWproject : à ouvrir dans Bitwig, Studio One, Cubase...")
                                        : u8 ("L'export a échoué."));
                          });
}

void ProjectSheet::importFile()
{
    chooser = std::make_unique<juce::FileChooser> (u8 ("Importer un projet"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto url = fc.getURLResult();
                              if (url.isEmpty()) return;
                              juce::String text;
                              if (url.isLocalFile()) text = url.getLocalFile().loadFileAsString();
                              else if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
                                  text = in->readEntireStreamAsString();
                              auto p = Project::fromJson (text);
                              if (! p) { say (u8 ("Ce fichier n'est pas un projet SLOOP Touch lisible.")); return; }
                              if (p->name.isEmpty()) p->name = url.getFileName().upToLastOccurrenceOf (".", false, false);
                              ctl.load (*p);
                              store::write (store::fileFor (p->name), *p);   // kept in the app too
                              name.setText (ctl.projectName, false);
                              say (u8 ("Importé : ") + p->name);
                              rebuildList();
                          });
}

juce::Rectangle<int> ProjectSheet::panel() const
{
    const int w = juce::jmin (getWidth(), 640), h = juce::jmin (getHeight() - 24, 520);
    return { (getWidth() - w) / 2, getHeight() - h, w, h };
}

void ProjectSheet::resized()
{
    auto p = panel().reduced (16);
    p.removeFromTop (34);
    auto top = p.removeFromTop (44);
    saveButton.setBounds (top.removeFromRight (130));
    top.removeFromRight (8);
    name.setBounds (top);
    p.removeFromTop (8);
    auto bottom = p.removeFromBottom (44);
    ok.setBounds (bottom.removeFromRight (70));
    bottom.removeFromRight (6);
    const int w = juce::jmin (130, (bottom.getWidth() - 4 * 6) / 5);
    for (auto* b : { &newButton, &exportButton, &importButton, &dawButton, &settingsButton })
    {
        b->setBounds (bottom.removeFromLeft (w)); bottom.removeFromLeft (6);
    }
    p.removeFromBottom (30);                             // the status line
    list.setBounds (p);
    const int rh = 44;
    listContent.setSize (p.getWidth(), juce::jmax (1, rows.size() * rh));
    for (int i = 0; i < rows.size(); ++i)
    {
        rows[i]->setBounds (0, i * rh, p.getWidth() - 92, rh - 6);
        deletes[i]->setBounds (p.getWidth() - 86, i * rh, 86, rh - 6);
    }
}

void ProjectSheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    auto p = panel();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (p.toFloat(), 10.0f);
    auto inner = p.reduced (16);
    g.setColour (theme::fg);
    g.setFont (theme::font (19.0f, true));
    g.drawText (u8 ("Projet"), inner.removeFromTop (28), juce::Justification::centredLeft);
    auto statusLine = list.getBounds().translated (0, list.getHeight()).withHeight (30);
    g.setColour (theme::dim);
    g.setFont (theme::font (12.5f));
    g.drawFittedText (status.isNotEmpty() ? status
                      : rows.isEmpty() ? u8 ("Aucun projet enregistré. Donne un nom et touche « Enregistrer ».")
                                       : u8 ("Touche un projet pour l'ouvrir. La session en cours est aussi gardée toute seule."),
                      statusLine, juce::Justification::centredLeft, 2);
}

void ProjectSheet::mouseUp (const juce::MouseEvent& e)
{
    if (! panel().contains (e.getPosition())) onClose();
}
} // namespace sloop
