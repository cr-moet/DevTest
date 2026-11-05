#pragma once

#include "PluginEditor.h"   // WindowButton, colours, noise texture, look and feel

//==============================================================================
// Borderless dialog window in the DevTest style (used for the options dialogs):
// title bar in the background colour with the red close button, no system frame
// and no system icon. The title bar moves the window; Escape closes it.
// The dialog is modal and deletes itself when it is closed.
//==============================================================================
class DevTestDialog final : public juce::ResizableWindow
{
public:
    static constexpr int titleHeight = 32;

    // Opens the dialog centred over 'centreAround' (may be nullptr)
    static DevTestDialog* show (const juce::String& title, std::unique_ptr<juce::Component> content,
                                juce::Component* centreAround)
    {
        auto* dialog = new DevTestDialog (title, std::move (content));

        if (centreAround != nullptr)
            dialog->centreAroundComponent (centreAround, dialog->getWidth(), dialog->getHeight());
        else
            dialog->centreWithSize (dialog->getWidth(), dialog->getHeight());

        dialog->setVisible (true);
        dialog->enterModalState (true, nullptr, false);
        return dialog;
    }

    ~DevTestDialog() override
    {
        clearContentComponent();
        setLookAndFeel (nullptr);
    }

    void close()
    {
        if (closing)
            return;

        closing = true;
        exitModalState (0);
        setVisible (false);
        juce::MessageManager::callAsync ([safe = SafePointer<DevTestDialog> (this)]
        {
            delete safe.getComponent();
        });
    }

    juce::BorderSize<int> getBorderThickness() const override         { return {}; }
    juce::BorderSize<int> getContentComponentBorder() const override  { return {}; }

    // Dialog: no task bar entry
    int getDesktopWindowStyleFlags() const override
    {
        return juce::ResizableWindow::getDesktopWindowStyleFlags() & ~juce::ComponentPeer::windowAppearsOnTaskbar;
    }

    void userTriedToCloseWindow() override   { close(); }   // Alt+F4

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return juce::ResizableWindow::keyPressed (key);
    }

private:
    //==========================================================================
    // Title bar + content
    class Frame final : public juce::Component
    {
    public:
        Frame (DevTestDialog& d, const juce::String& t, std::unique_ptr<juce::Component> c)
            : dialog (d), title (t), content (std::move (c))
        {
            closeButton.onClick = [this] { dialog.close(); };
            addAndMakeVisible (closeButton);
            addAndMakeVisible (*content);
            updateSize();
        }

        void paint (juce::Graphics& g) override
        {
            using namespace DevTestColours;

            g.fillAll (background);
            NoiseTexture::draw (g, getLocalBounds());

            // Title
            g.setColour (text);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (titleHeight).withTrimmedLeft (14).withTrimmedRight (40),
                        juce::Justification::centredLeft);

            g.setColour (text.withAlpha (0.2f));
            g.drawHorizontalLine (titleHeight - 1, 0.0f, (float) getWidth());
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // Thin frame, so the dialog stands out against the main window
            g.setColour (DevTestColours::text.withAlpha (0.45f));
            g.drawRect (getLocalBounds(), 1);
        }

        void resized() override
        {
            closeButton.setBounds (getWidth() - 6 - 16, 6, 16, 16);
            content->setBounds (getLocalBounds().withTrimmedTop (titleHeight));
        }

        // Content changed its size (e.g. the audio settings after another device type)
        void childBoundsChanged (juce::Component* child) override
        {
            if (child == content.get() && ! updating)
                updateSize();
        }

        //======================================================================
        // Moving the window: the position follows the mouse pointer on the screen
        // (polled while the button is held, so fast movements cannot lose it)
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.y >= titleHeight)
                return;

            dragging         = true;
            dragStartWindow  = dialog.getScreenPosition();
            dragStartMouse   = juce::Desktop::getMousePosition();
            dragTimer.startTimerHz (120);
        }

        void mouseDrag (const juce::MouseEvent&) override  { updateDrag(); }
        void mouseUp (const juce::MouseEvent&) override    { updateDrag(); dragging = false; dragTimer.stopTimer(); }

    private:
        void updateSize()
        {
            const juce::ScopedValueSetter<bool> svs (updating, true);
            setSize (content->getWidth(), content->getHeight() + titleHeight);
        }

        void updateDrag()
        {
            if (! dragging)
                return;

            if (! juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown())
            {
                dragging = false;
                dragTimer.stopTimer();
                return;
            }

            const auto target = dragStartWindow + (juce::Desktop::getMousePosition() - dragStartMouse);
            if (dialog.getScreenPosition() != target)
                dialog.setTopLeftPosition (target);
        }

        DevTestDialog& dialog;
        juce::String title;
        std::unique_ptr<juce::Component> content;
        WindowButton closeButton { WindowButton::Type::close, DevTestColours::accent };
        bool updating = false;

        bool dragging = false;
        juce::Point<int> dragStartWindow, dragStartMouse;
        juce::TimedCallback dragTimer { [this] { updateDrag(); } };
    };

    //==========================================================================
    DevTestDialog (const juce::String& title, std::unique_ptr<juce::Component> content)
        : juce::ResizableWindow (title, DevTestColours::background, false)
    {
        setLookAndFeel (&lookAndFeel);   // DevTest look also inside a host (plugin)
        setUsingNativeTitleBar (false);
        setResizable (false, false);
        setDropShadowEnabled (true);
        setContentOwned (new Frame (*this, title, std::move (content)), true);
        addToDesktop (getDesktopWindowStyleFlags());
        setWantsKeyboardFocus (true);
    }

    DevTestLookAndFeel lookAndFeel;
    bool closing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevTestDialog)
};
