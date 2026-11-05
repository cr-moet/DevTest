// Custom standalone application for DevTest.
//
// Instead of JUCE's default standalone window (grey frame, title bar and an
// "Options" button) DevTest uses a borderless window that shows only the plugin
// editor. Close, minimise and the options menu (audio/MIDI settings) are drawn
// by the editor itself, in the DevTest colours.
//
// Enabled with JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 in CMakeLists.txt.
// JUCE's StandalonePluginHolder still does the audio device handling and saves
// the plugin state and the audio settings.

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginEditor.h"

// JUCE's standalone helper classes (StandalonePluginHolder: audio device, settings)
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>


#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

namespace
{
    //==========================================================================
    // Borderless main window that contains only the plugin editor
    class DevTestWindow final : public juce::ResizableWindow
    {
    public:
        DevTestWindow (juce::StandalonePluginHolder& h, juce::PropertySet& s)
            : juce::ResizableWindow (JucePlugin_Name, DevTestColours::background, false),
              holder (h), settings (s)
        {
            setUsingNativeTitleBar (false);
            setResizable (false, false);   // the editor has its own resize corner
            setDropShadowEnabled (true);
            addToDesktop (getDesktopWindowStyleFlags());

            editor.reset (holder.processor->createEditorAndMakeActive());

            if (auto* devTestEditor = dynamic_cast<AudioPluginAudioProcessorEditor*> (editor.get()))
            {
                devTestEditor->setWindowControls (
                    [] { juce::JUCEApplication::quit(); },              // close
                    [this] { setMinimised (true); },                     // minimise
                    [this] { holder.showAudioSettingsDialog(); });       // options
            }

            setContentNonOwned (editor.get(), true);

            // Restore the last window position (or centre on the screen)
            const auto x = settings.getIntValue ("windowX", -100000);
            const auto y = settings.getIntValue ("windowY", -100000);

            if (x != -100000 && y != -100000)
                setTopLeftPosition (x, y);
            else
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
        }

        ~DevTestWindow() override
        {
            settings.setValue ("windowX", getX());
            settings.setValue ("windowY", getY());

            clearContentComponent();
            holder.processor->editorBeingDeleted (editor.get());
            editor.reset();
        }

        // No frame around the editor
        juce::BorderSize<int> getBorderThickness() const override         { return {}; }
        juce::BorderSize<int> getContentComponentBorder() const override  { return {}; }

        // Appears in the task bar and can be minimised/restored there
        int getDesktopWindowStyleFlags() const override
        {
            return juce::ResizableWindow::getDesktopWindowStyleFlags() | juce::ComponentPeer::windowHasMinimiseButton;
        }

        // Alt+F4 / closing from the task bar
        void userTriedToCloseWindow() override   { juce::JUCEApplication::quit(); }


    private:
        juce::StandalonePluginHolder& holder;
        juce::PropertySet& settings;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
    };

    //==========================================================================
    class DevTestStandaloneApp final : public juce::JUCEApplication
    {
    public:
        DevTestStandaloneApp()
        {
            juce::PropertiesFile::Options options;
            options.applicationName     = JucePlugin_Name;
            options.filenameSuffix      = ".settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX || JUCE_BSD
            options.folderName          = "~/.config";
           #else
            options.folderName          = "";
           #endif
            appProperties.setStorageParameters (options);
        }

        const juce::String getApplicationName() override              { return JucePlugin_Name; }
        const juce::String getApplicationVersion() override           { return JucePlugin_VersionString; }
        bool moreThanOneInstanceAllowed() override                    { return true; }
        void anotherInstanceStarted (const juce::String&) override    {}

        void initialise (const juce::String&) override
        {
            // DevTest colours also for dialogs (e.g. the audio settings)
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            auto* settings = appProperties.getUserSettings();

            // A measurement tool needs its inputs: do not mute them by default
            // (can still be changed in the audio settings)
            const bool firstStart = ! settings->containsKey ("shouldMuteInput");

            holder = std::make_unique<juce::StandalonePluginHolder> (settings, false, juce::String{}, nullptr,
                                                                     juce::Array<juce::StandalonePluginHolder::PluginInOuts>{},
                                                                     false);
            if (firstStart)
                holder->getMuteInputValue() = false;

            if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
                return;   // no screen available

            window = std::make_unique<DevTestWindow> (*holder, *settings);
        }

        void shutdown() override
        {
            if (holder != nullptr)
                holder->savePluginState();

            window.reset();
            holder.reset();
            appProperties.saveIfNeeded();

            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        void systemRequestedQuit() override
        {
            if (holder != nullptr)
                holder->savePluginState();

            quit();
        }

    private:
        DevTestLookAndFeel lookAndFeel;
        juce::ApplicationProperties appProperties;
        std::unique_ptr<juce::StandalonePluginHolder> holder;
        std::unique_ptr<DevTestWindow> window;
    };
}

// Entry point used by JUCE's standalone wrapper
juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication()   { return new DevTestStandaloneApp(); }

#endif
