#pragma once

#include "PluginProcessor.h"
#include "ScopeComponent.h"
#include "DevTestLookAndFeel.h"

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupLabel  (juce::Label&, const juce::String& text);
    void setupSlider (juce::Slider&);
    void setupCombo  (juce::ComboBox&, const juce::String& paramID);

    AudioPluginAudioProcessor& processorRef;
    DevTestLookAndFeel lookAndFeel;

    ScopeComponent scope;

    // Oszilloskop
    juce::GroupComponent scopeGroup { {}, "OSZILLOSKOP" };
    juce::Label    srcLLabel, srcRLabel, timeLabel;
    juce::ComboBox srcLBox, srcRBox;
    juce::Slider   timeSlider;
    juce::ToggleButton triggerButton     { "Trigger (steigende Flanke)" };
    juce::ToggleButton passThroughButton { "Eingang zum Ausgang durchleiten" };

    // Generator
    juce::GroupComponent genGroup { {}, "SINUSGENERATOR" };
    juce::ToggleButton genOnButton { "Generator an" };
    juce::Label    freqLabel, levelLabel, genOutLabel;
    juce::Slider   freqSlider, levelSlider;
    juce::ComboBox genOutBox;

    // Hinweis fuer Ein-/Ausgangswahl im Standalone
    juce::Label ioHint;

    std::unique_ptr<ComboAttachment>  srcLAttach, srcRAttach, genOutAttach;
    std::unique_ptr<SliderAttachment> timeAttach, freqAttach, levelAttach;
    std::unique_ptr<ButtonAttachment> triggerAttach, passAttach, genOnAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
