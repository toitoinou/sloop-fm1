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
        // --snapshot out.png WIDTHxHEIGHT [session|grid|pads|soundN|trackN]: draws the screen into a PNG and quits
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
    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        explicit Window (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff151618), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new sloop::MainComponent(), true);
           #if JUCE_ANDROID || JUCE_IOS
            setFullScreen (true);
           #else
            setResizable (true, true);
            setResizeLimits (360, 360, 4000, 4000);
            centreWithSize (getWidth(), getHeight());
           #endif
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (SloopTouchApplication)
