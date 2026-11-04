#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Farben von DevTest
//==============================================================================
namespace DevTestColours
{
    inline const juce::Colour background { 0xfff2ebd6 };   // Hintergrund
    inline const juce::Colour text       { 0xff292929 };   // Schrift
    inline const juce::Colour accent     { 0xffd52925 };   // Spezielle Elemente
    inline const juce::Colour traceL     { 0xff292929 };   // Oszi Kanal L
    inline const juce::Colour traceR     { 0xffd52925 };   // Oszi Kanal R

    // Abgeleitete Toene fuer Flaechen und Raster
    inline const juce::Colour panel      = background.darker (0.06f);
    inline const juce::Colour grid       = text.withAlpha (0.12f);
    inline const juce::Colour gridStrong = text.withAlpha (0.35f);
}

//==============================================================================
class DevTestLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DevTestLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              DevTestColours::background,                 // windowBackground
              DevTestColours::panel,                      // widgetBackground
              DevTestColours::background,                 // menuBackground
              DevTestColours::text.withAlpha (0.4f),      // outline
              DevTestColours::text,                       // defaultText
              DevTestColours::accent,                     // defaultFill
              DevTestColours::background,                 // highlightedText
              DevTestColours::accent,                     // highlightedFill
              DevTestColours::text })                     // menuText
    {
        using namespace DevTestColours;

        setColour (juce::Slider::thumbColourId,               accent);
        setColour (juce::Slider::trackColourId,               accent);
        setColour (juce::Slider::backgroundColourId,          text.withAlpha (0.15f));
        setColour (juce::Slider::rotarySliderFillColourId,    accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, text.withAlpha (0.15f));
        setColour (juce::Slider::textBoxTextColourId,         text);
        setColour (juce::Slider::textBoxBackgroundColourId,   background);
        setColour (juce::Slider::textBoxOutlineColourId,      text.withAlpha (0.25f));

        setColour (juce::ToggleButton::textColourId,          text);
        setColour (juce::ToggleButton::tickColourId,          accent);
        setColour (juce::ToggleButton::tickDisabledColourId,  text.withAlpha (0.5f));

        setColour (juce::ComboBox::backgroundColourId,        background);
        setColour (juce::ComboBox::textColourId,              text);
        setColour (juce::ComboBox::outlineColourId,           text.withAlpha (0.4f));
        setColour (juce::ComboBox::arrowColourId,             accent);

        setColour (juce::GroupComponent::outlineColourId,     text.withAlpha (0.3f));
        setColour (juce::GroupComponent::textColourId,        accent);

        setColour (juce::Label::textColourId,                 text);
        setColour (juce::TextEditor::textColourId,            text);
        setColour (juce::TextEditor::backgroundColourId,      background);
        setColour (juce::TextEditor::highlightColourId,       accent.withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId,       accent);
    }
};
