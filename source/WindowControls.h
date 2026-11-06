#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Small window buttons for the borderless windows: options (three dots),
// minimise, close
//==============================================================================
class WindowButton final : public juce::Button
{
public:
    enum class Type { options, minimise, close };

    WindowButton (Type t, juce::Colour c)
        : juce::Button (t == Type::options ? "Options" : (t == Type::minimise ? "Minimise" : "Close")),
          type (t), colour (c)
    {
        setTooltip (getName());
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);

        if (highlighted || down)
        {
            g.setColour (colour.withAlpha (down ? 0.18f : 0.10f));
            g.fillRoundedRectangle (area, 3.0f);
        }

        g.setColour (colour);
        const auto c = area.getCentre();
        const float s = juce::jmin (area.getWidth(), area.getHeight()) * 0.28f;   // half size of the symbol

        switch (type)
        {
            case Type::options:   // three dots on top of each other
                for (int i = -1; i <= 1; ++i)
                    g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ c.x, c.y + (float) i * s * 1.1f }));
                break;

            case Type::minimise:
                g.fillRect (juce::Rectangle<float> (2.0f * s, 1.6f).withCentre ({ c.x, c.y + s * 0.8f }));
                break;

            case Type::close:
                g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, 1.6f);
                g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 1.6f);
                break;
        }
    }

private:
    Type type;
    juce::Colour colour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowButton)
};

//==============================================================================
// Moves the top-level window of a component with the mouse. The position is
// taken directly from the mouse pointer on the screen and polled while the button
// is held, so the window can never "lose" the mouse, even with fast movements.
// Call start() from mouseDown, update() from mouseDrag and stop() from mouseUp.
//==============================================================================
class WindowDragger final
{
public:
    explicit WindowDragger (juce::Component& c) : component (c) {}

    void start()
    {
        if (auto* window = component.getTopLevelComponent())
        {
            dragging    = true;
            startWindow = window->getScreenPosition();
            startMouse  = juce::Desktop::getMousePosition();
            timer.startTimerHz (120);   // keeps following even if mouse events get lost
        }
    }

    void update()
    {
        if (! dragging)
            return;

        // Mouse button released (also outside the window): stop
        if (! juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown())
        {
            cancel();
            return;
        }

        if (auto* window = component.getTopLevelComponent())
        {
            const auto target = startWindow + (juce::Desktop::getMousePosition() - startMouse);
            if (window->getScreenPosition() != target)
                window->setTopLeftPosition (target);
        }
    }

    void stop()
    {
        update();
        cancel();
    }

private:
    void cancel()
    {
        dragging = false;
        timer.stopTimer();
    }

    juce::Component& component;
    bool dragging = false;
    juce::Point<int> startWindow, startMouse;   // screen coordinates
    juce::TimedCallback timer { [this] { update(); } };

    JUCE_DECLARE_NON_COPYABLE (WindowDragger)
};
