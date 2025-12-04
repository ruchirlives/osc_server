/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
class VSTHostMidiRecorderApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    VSTHostMidiRecorderApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    //==============================================================================
    void initialise (const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);

        mainWindow.reset (new MainWindow (getApplicationName()));
        // Create the system tray icon
        trayIconComponent.reset(new TrayIconComponent());

        
	    // Make window stay on top
	    mainWindow -> setAlwaysOnTop(true);
    }

    void shutdown() override
    {
        // Add your application's shutdown code here..
        trayIconComponent = nullptr;
        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            setBounds(50, 50, 750, 460);
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            // Instead of quitting the application, hide the main window
            setVisible(false);
            //JUCEApplication::getInstance()->systemRequestedQuit();
        }

        /* Note: Be careful if you override any DocumentWindow methods - the base
           class uses a lot of them, so by overriding you might break its functionality.
           It's best to do all your work in your content component instead, but if
           you really have to override any DocumentWindow methods, make sure your
           subclass also calls the superclass's method.
        */



    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    class TrayIconComponent : public juce::SystemTrayIconComponent
    {
    public:
        TrayIconComponent()
        {
            // Set an icon for the system tray
            trayIconImage = juce::ImageCache::getFromMemory(BinaryData::icon_png, BinaryData::icon_pngSize);

            if (!trayIconImage.isValid())
            {
                DBG("Error: Failed to load tray icon image");
                return;
            }

            setIconImage(trayIconImage, trayIconImage);
            setIconTooltip("DAWSERVER");
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.mods.isLeftButtonDown())
            {
                // Toggle the visibility of the main window on left-click
                if (auto* mainApp = dynamic_cast<VSTHostMidiRecorderApplication*>(juce::JUCEApplication::getInstance()))
                {
                    if (mainApp->mainWindow != nullptr)
                    {
                        bool isVisible = mainApp->mainWindow->isVisible();
                        mainApp->mainWindow->setVisible(!isVisible);

                        if (!isVisible)
                            mainApp->mainWindow->toFront(true);
                    }
                }
            }
            else if (event.mods.isRightButtonDown())
            {
                // Show a context menu on right-click
                juce::PopupMenu menu;
                menu.addItem("Restore", [this]()
                    {
                        if (auto* mainApp = dynamic_cast<VSTHostMidiRecorderApplication*>(juce::JUCEApplication::getInstance()))
                        {
                            if (mainApp->mainWindow != nullptr)
                            {
                                mainApp->mainWindow->setVisible(true);
                                mainApp->mainWindow->toFront(true);
                            }
                        }
                    });

                menu.addItem("Quit", []()
                    {
                        juce::JUCEApplication::getInstance()->systemRequestedQuit();
                    });

                menu.show();
            }
        }

    private:
        juce::Image trayIconImage;
    };


private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIconComponent> trayIconComponent;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (VSTHostMidiRecorderApplication)
