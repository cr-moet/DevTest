#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// DevTest colours
//==============================================================================
namespace DevTestColours
{
    inline const juce::Colour background { 0xfff2ebd6 };   // background
    inline const juce::Colour text       { 0xff292929 };   // text
    inline const juce::Colour accent     { 0xffd52925 };   // special elements
    inline const juce::Colour traceL     { 0xff292929 };   // scope channel L
    inline const juce::Colour traceR     { 0xffd52925 };   // scope channel R

    // Derived tones for panels and grid lines
    inline const juce::Colour panel      = background.darker (0.06f);
    inline const juce::Colour grid       = text.withAlpha (0.12f);
    inline const juce::Colour gridStrong = text.withAlpha (0.35f);
}

//==============================================================================
// Background texture: normally distributed (Gaussian) white noise.
// Each pixel is white for positive and black for negative noise values, with an
// alpha proportional to the magnitude, so the average brightness of the
// background colour stays unchanged. The tile is drawn repeatedly with
// 'noiseOpacity' over the background.
//==============================================================================
namespace NoiseTexture
{
    inline constexpr float noiseOpacity = 0.042f;   // overall strength of the noise layer
    inline constexpr int   tileSize     = 512;     // large enough that repetition is not visible

    inline juce::Image createTile()
    {
        juce::Image tile (juce::Image::ARGB, tileSize, tileSize, true);
        juce::Random random (0x5eed);  // fixed seed -> same texture on every start

        // Box-Muller transform: two uniform random numbers -> two Gaussian values
        const auto nextGaussianPair = [&random] (float& a, float& b)
        {
            const float u1 = juce::jmax (1.0e-7f, random.nextFloat());
            const float u2 = random.nextFloat();
            const float r  = std::sqrt (-2.0f * std::log (u1));
            a = r * std::cos (juce::MathConstants<float>::twoPi * u2);
            b = r * std::sin (juce::MathConstants<float>::twoPi * u2);
        };

        juce::Image::BitmapData pixels (tile, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < tileSize; ++y)
        {
            for (int x = 0; x < tileSize; x += 2)
            {
                float n[2];
                nextGaussianPair (n[0], n[1]);

                for (int k = 0; k < 2; ++k)
                {
                    // standard deviation 1 -> alpha 0.5, clipped at 3 sigma
                    const float alpha = juce::jlimit (0.0f, 1.0f, std::abs (n[k]) * 0.5f);
                    const auto colour = (n[k] >= 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha (alpha);
                    pixels.setPixelColour (x + k, y, colour);
                }
            }
        }

        return tile;
    }

    // Shared tile, created once on first use
    inline const juce::Image& getTile()
    {
        static const juce::Image tile = createTile();
        return tile;
    }

    // Fills 'area' with the noise texture at 'noiseOpacity'
    inline void draw (juce::Graphics& g, juce::Rectangle<int> area)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.setTiledImageFill (getTile(), 0, 0, noiseOpacity);
        g.fillRect (area);
    }
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
