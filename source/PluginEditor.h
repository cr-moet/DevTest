#pragma once

#include "PluginProcessor.h"
#include "ScopeComponent.h"
#include "SpectrumComponent.h"
#include "KneeComponent.h"
#include "SweepComponent.h"
#include "AttRelComponent.h"
#include "DevTestLookAndFeel.h"

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

//==============================================================================
// Helper functions for consistent controls
//==============================================================================
namespace UiHelpers
{
    inline void setupLabel (juce::Component& parent, juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (14.0f)));
        parent.addAndMakeVisible (label);
    }

    inline void setupSlider (juce::Component& parent, juce::Slider& slider)
    {
        parent.addAndMakeVisible (slider);
        slider.setColour (juce::Slider::textBoxTextColourId,       DevTestColours::text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, DevTestColours::background);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 96, 24);
        slider.setTextBoxIsEditable (true); // values can be typed in directly, e.g. "440" or "1k"
    }

    inline void setupCombo (juce::Component& parent, juce::ComboBox& box,
                            juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
            box.addItemList (choice->choices, 1);

        parent.addAndMakeVisible (box);
    }
}

//==============================================================================
// "Scope" tab: oscilloscope + time/div + trigger
//==============================================================================
class ScopePage final : public juce::Component
{
public:
    explicit ScopePage (AudioPluginAudioProcessor& p);
    void resized() override;

private:
    ScopeComponent scope;
    juce::Label  timeLabel;
    juce::Slider timeSlider;
    juce::ToggleButton triggerButton { "Trigger (rising edge)" };

    std::unique_ptr<SliderAttachment> timeAttach;
    std::unique_ptr<ButtonAttachment> triggerAttach;
};

//==============================================================================
// "FFT" tab: spectrum + settings
//==============================================================================
class FftPage final : public juce::Component
{
public:
    explicit FftPage (AudioPluginAudioProcessor& p);
    void resized() override;

private:
    SpectrumComponent spectrum;
    juce::Label    sizeLabel, windowLabel, avgLabel, rangeLabel, maxFreqLabel;
    juce::ComboBox sizeBox, windowBox, avgBox, rangeBox, maxFreqBox;

    std::unique_ptr<ComboAttachment> sizeAttach, windowAttach, avgAttach, rangeAttach, maxFreqAttach;
};

//==============================================================================
// "Knee" tab: compressor transfer curve. A test tone rises slowly from
// -60 dBFS to 0 dBFS; the levels returned on inputs L and R are plotted
// against the sent level.
//==============================================================================
class KneePage final : public juce::Component,
                       private juce::Timer
{
public:
    explicit KneePage (AudioPluginAudioProcessor& p);
    void resized() override;

private:
    void timerCallback() override;

    AudioPluginAudioProcessor& processor;
    KneeComponent knee;

    juce::TextButton startButton { "START" };
    juce::Label    waveLabel, freqLabel, durationLabel, outputLabel;
    juce::ComboBox waveBox, outputBox;
    juce::Slider   freqSlider, durationSlider;

    std::unique_ptr<ComboAttachment>  waveAttach, outputAttach;
    std::unique_ptr<SliderAttachment> freqAttach, durationAttach;
};

//==============================================================================
// "Sweep" tab: frequency response (amplitude over frequency), measured either
// live with a sine sweep or from impulse responses (Dirac impulses).
//==============================================================================
class SweepPage final : public juce::Component,
                        private juce::Timer
{
public:
    explicit SweepPage (AudioPluginAudioProcessor& p);
    void resized() override;

private:
    void timerCallback() override;

    AudioPluginAudioProcessor& processor;
    SweepComponent sweep;

    juce::TextButton startButton { "START" };
    juce::Label    modeLabel, lawLabel, endLabel, durationLabel, levelLabel, outputLabel;
    juce::ComboBox modeBox, lawBox, endBox, outputBox;
    juce::Slider   durationSlider, levelSlider;

    // Impulse mode settings (shown instead of law and duration); "Continuous" for both modes
    juce::Label    intervalLabel, averagesLabel;
    juce::ComboBox intervalBox, averagesBox;
    juce::ToggleButton continuousButton { "Continuous" };

    // Display settings (right column)
    juce::Label    rangeLabel, smoothLabel, gateLabel, noteLabel;
    juce::ComboBox rangeBox, smoothBox, gateBox;
    juce::ToggleButton normButton { "Normalise to 1 kHz" };

    std::unique_ptr<ComboAttachment>  modeAttach, lawAttach, endAttach, outputAttach, rangeAttach, smoothAttach, gateAttach,
                                      intervalAttach, averagesAttach;
    std::unique_ptr<SliderAttachment> durationAttach, levelAttach;
    std::unique_ptr<ButtonAttachment> normAttach, continuousAttach;
};

//==============================================================================
// "Att/Rel" tab: attack and release time of a compressor, measured with two
// consecutive sine bursts.
//==============================================================================
class AttRelPage final : public juce::Component,
                         private juce::Timer
{
public:
    explicit AttRelPage (AudioPluginAudioProcessor& p);
    void resized() override;

private:
    void timerCallback() override;

    AudioPluginAudioProcessor& processor;
    AttRelComponent display;

    juce::TextButton startButton { "START" };
    juce::Label  freqLabel, outputLabel, burst1Label, burst2Label, len1Label, level1Label, len2Label, level2Label;
    juce::Slider freqSlider, len1Slider, level1Slider, len2Slider, level2Slider;
    juce::ComboBox outputBox;

    std::unique_ptr<SliderAttachment> freqAttach, len1Attach, level1Attach, len2Attach, level2Attach;
    std::unique_ptr<ComboAttachment>  outputAttach;
};

//==============================================================================
// Small buttons for the standalone window: options (three dots), minimise, close
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
};

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Standalone only: shows the window buttons (close, minimise, options) in the
    // header and makes the header draggable to move the borderless window
    void setWindowControls (std::function<void()> onClose, std::function<void()> onMinimise,
                            std::function<void()> onOptions);

    // Standalone: true for the part of the header that moves the window (title bar
    // area without the window buttons). Used by the window for the system's own
    // window dragging (Windows); elsewhere the editor moves the window itself.
    bool isCaptionArea (juce::Point<int> position) const;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void showPage (int index);
    void layoutTitle();

    static constexpr int   headerHeight = 56;
    static constexpr float titleBorder  = 5.0f;   // frame thickness around the program name

    AudioPluginAudioProcessor& processorRef;
    DevTestLookAndFeel lookAndFeel;

    // Tabs
    juce::TextButton scopeTab { "Scope" }, fftTab { "FFT" }, kneeTab { "Knee" }, sweepTab { "Sweep" }, attRelTab { "Att/Rel" };
    ScopePage scopePage;
    FftPage   fftPage;
    KneePage  kneePage;
    SweepPage sweepPage;
    AttRelPage attRelPage;
    int currentPage = 0;

    // Generator (only shown with Scope and FFT)
    juce::GroupComponent genGroup { {}, "SIGNAL GENERATOR" };
    juce::ToggleButton genOnButton { "Generator on" };
    juce::Label    waveLabel, freqLabel, levelLabel, genOutLabel;
    juce::ComboBox waveBox, genOutBox;
    juce::Slider   freqSlider, levelSlider;

    // Standalone window buttons
    WindowButton optionsButton  { WindowButton::Type::options,  DevTestColours::text };
    WindowButton minimiseButton { WindowButton::Type::minimise, DevTestColours::text };
    WindowButton closeButton    { WindowButton::Type::close,    DevTestColours::accent };
    bool hasWindowControls = false;
    bool draggingWindow = false;
    juce::Point<int> dragStartWindowPos, dragStartMouse;   // screen coordinates

    // Program name in a framed box (text is drawn as a path so it can be centred exactly)
    juce::Path titlePath;
    juce::Rectangle<float> titleBox;

    std::unique_ptr<ComboAttachment>  genOutAttach, waveAttach;
    std::unique_ptr<SliderAttachment> freqAttach, levelAttach;
    std::unique_ptr<ButtonAttachment> genOnAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
