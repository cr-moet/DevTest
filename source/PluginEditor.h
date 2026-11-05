#pragma once

#include "PluginProcessor.h"
#include "ScopeComponent.h"
#include "SpectrumComponent.h"
#include "KneeComponent.h"
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

    juce::TextButton startButton { "Start test" };
    juce::Label    waveLabel, freqLabel, durationLabel, outputLabel;
    juce::ComboBox waveBox, outputBox;
    juce::Slider   freqSlider, durationSlider;

    std::unique_ptr<ComboAttachment>  waveAttach, outputAttach;
    std::unique_ptr<SliderAttachment> freqAttach, durationAttach;
};

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showPage (int index);
    void layoutTitle();

    static constexpr int   headerHeight = 56;
    static constexpr float titleBorder  = 5.0f;   // frame thickness around the program name

    AudioPluginAudioProcessor& processorRef;
    DevTestLookAndFeel lookAndFeel;

    // Tabs
    juce::TextButton scopeTab { "Scope" }, fftTab { "FFT" }, kneeTab { "Knee" };
    ScopePage scopePage;
    FftPage   fftPage;
    KneePage  kneePage;

    // Channels (shared by Scope and FFT)
    juce::GroupComponent channelGroup { {}, "CHANNELS" };
    juce::Label    srcLLabel, srcRLabel;
    juce::ComboBox srcLBox, srcRBox;
    juce::ToggleButton passThroughButton { "Pass input through to output" };

    // Generator (shared by Scope and FFT)
    juce::GroupComponent genGroup { {}, "SIGNAL GENERATOR" };
    juce::ToggleButton genOnButton { "Generator on" };
    juce::Label    waveLabel, freqLabel, levelLabel, genOutLabel;
    juce::ComboBox waveBox, genOutBox;
    juce::Slider   freqSlider, levelSlider;

    juce::Label ioHint;

    // Program name in a framed box (text is drawn as a path so it can be centred exactly)
    juce::Path titlePath;
    juce::Rectangle<float> titleBox;

    std::unique_ptr<ComboAttachment>  srcLAttach, srcRAttach, genOutAttach, waveAttach;
    std::unique_ptr<SliderAttachment> freqAttach, levelAttach;
    std::unique_ptr<ButtonAttachment> passAttach, genOnAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
