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
#include "DevTestDialog.h"

// JUCE's standalone helper classes (StandalonePluginHolder: audio device, settings)
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>


#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

namespace
{
    //==========================================================================
    // Audio settings (device, sample rate, buffer size, channels) for the
    // DevTest dialog; same content as JUCE's standard standalone dialog
    class AudioSettingsComponent final : public juce::Component
    {
    public:
        AudioSettingsComponent (juce::StandalonePluginHolder& h, int maxInputs, int maxOutputs)
            : holder (h),
              deviceSelector (h.deviceManager, 0, maxInputs, 0, maxOutputs, false, false, true, false)
        {
            addAndMakeVisible (deviceSelector);

            muteButton.setClickingTogglesState (true);
            muteButton.getToggleStateValue().referTo (holder.getMuteInputValue());
            if (holder.getProcessorHasPotentialFeedbackLoop())
                addAndMakeVisible (muteButton);

            setSize (520, 400);
            setToRecommendedSize();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (DevTestColours::background);
            NoiseTexture::draw (g, getLocalBounds());
        }

        void resized() override
        {
            const juce::ScopedValueSetter<bool> svs (isResizing, true);
            auto r = getLocalBounds().reduced (0, 8);

            if (muteButton.isVisible())
            {
                auto row = r.removeFromTop (deviceSelector.getItemHeight());
                muteButton.setBounds (row.withTrimmedLeft (row.proportionOfWidth (0.35f)).withWidth (row.proportionOfWidth (0.60f)));
                r.removeFromTop (deviceSelector.getItemHeight() / 2);
            }

            deviceSelector.setBounds (r);
        }

        void childBoundsChanged (juce::Component* child) override
        {
            if (! isResizing && child == &deviceSelector)
                setToRecommendedSize();
        }

    private:
        void setToRecommendedSize()
        {
            const int extra = muteButton.isVisible() ? deviceSelector.getItemHeight() + deviceSelector.getItemHeight() / 2 : 0;
            setSize (getWidth(), deviceSelector.getHeight() + extra + 16);
        }

        juce::StandalonePluginHolder& holder;
        juce::AudioDeviceSelectorComponent deviceSelector;
        juce::ToggleButton muteButton { "Mute audio input (feedback loop)" };
        bool isResizing = false;
    };

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
                    [this] { showAudioSettings(); });                    // options menu
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

        // Audio settings in the DevTest dialog (instead of the standard Windows dialog)
        void showAudioSettings()
        {
            int maxInputs = 2, maxOutputs = 2;
            if (auto* bus = holder.processor->getBus (true, 0))   maxInputs  = juce::jmax (0, bus->getDefaultLayout().size());
            if (auto* bus = holder.processor->getBus (false, 0))  maxOutputs = juce::jmax (0, bus->getDefaultLayout().size());

            DevTestDialog::show ("Audio Settings", std::make_unique<AudioSettingsComponent> (holder, maxInputs, maxOutputs), this);
        }


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
