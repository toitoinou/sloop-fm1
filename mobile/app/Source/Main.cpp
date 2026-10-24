// SPDX-License-Identifier: GPL-3.0-only
#include "MainComponent.h"

#ifndef JUCE_APPLICATION_NAME_STRING                 // the CMake build defines them; the Projucer one (Android) does not
 #define JUCE_APPLICATION_NAME_STRING "SLOOP Touch"
 #define JUCE_APPLICATION_VERSION_STRING JUCE_STRINGIFY (JUCE_APP_VERSION)
#endif

class SloopTouchApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& args) override
    {
        // --snapshot out.png WIDTHxHEIGHT [session|grid|pads|soundN|trackN|project]: draws the screen into a PNG and quits
        // (to look at the layout without a device, e.g. in CI)
        auto a = juce::StringArray::fromTokens (args, true);
        if (a.contains ("--selftest"))
        {
            setApplicationReturnValue (sloop::runSelfTest());
            quit();
            return;
        }
        if (const int i = a.indexOf ("--snapshot"); i >= 0 && i + 2 < a.size())
        {
            sloop::MainComponent ui;
            ui.setSize (a[i + 2].upToFirstOccurrenceOf ("x", false, false).getIntValue(),
                        a[i + 2].fromFirstOccurrenceOf ("x", false, false).getIntValue());
            if (i + 3 < a.size()) ui.showTabNamed (a[i + 3]);
            const auto image = ui.createComponentSnapshot (ui.getLocalBounds());
            juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (a[i + 1]);
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (image, stream);
            setApplicationReturnValue (stream.openedOk() ? 0 : 1);
            quit();
            return;
        }
        window = std::make_unique<Window> (getApplicationName());
    }
    // the session is kept when the app goes to the background (Android may end it there) and when it quits
    void suspended() override { autosave(); }
    void shutdown() override { autosave(); window = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    void autosave()
    {
        if (window != nullptr)
            if (auto* ui = dynamic_cast<sloop::MainComponent*> (window->getContentComponent())) ui->autosave();
    }
    class Window : public juce::DocumentWindow
    {
    public:
        explicit Window (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff151618), DocumentWindow::allButtons)
        {
            setContentOwned (new sloop::MainComponent(), true);
           #if JUCE_ANDROID || JUCE_IOS
            // no title bar on a phone, and the whole screen (the status and navigation bars hidden, a swipe from the
            // edge shows them for a moment)
            setUsingNativeTitleBar (false);
            setTitleBarHeight (0);
            setFullScreen (true);
           #else
            setUsingNativeTitleBar (true);
            setResizable (true, true);
            setResizeLimits (360, 360, 4000, 4000);
            centreWithSize (getWidth(), getHeight());
           #endif
            setVisible (true);
           #if JUCE_ANDROID || JUCE_IOS
            juce::Desktop::getInstance().setKioskModeComponent (this, false);
           #endif
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (SloopTouchApplication)
