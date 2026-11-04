#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), scope (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (scope);

    // ---------------- Oszilloskop ----------------
    addAndMakeVisible (scopeGroup);

    setupLabel (srcLLabel, "Kanal L");
    setupLabel (srcRLabel, "Kanal R");
    setupLabel (timeLabel, "Zeit / Div");

    setupCombo (srcLBox, ParamIDs::scopeSourceL);
    setupCombo (srcRBox, ParamIDs::scopeSourceR);

    setupSlider (timeSlider);

    addAndMakeVisible (triggerButton);
    addAndMakeVisible (passThroughButton);

    // ---------------- Generator ----------------
    addAndMakeVisible (genGroup);
    addAndMakeVisible (genOnButton);

    setupLabel (freqLabel,   "Frequenz");
    setupLabel (levelLabel,  "Amplitude");
    setupLabel (genOutLabel, "Ausgang");

    setupSlider (freqSlider);
    setupSlider (levelSlider);
    setupCombo  (genOutBox, ParamIDs::genOutput);

    // ---------------- Hinweis Ein-/Ausgaenge ----------------
    const bool isStandalone = processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    ioHint.setText (isStandalone
                        ? "Audio-Interface und Ein-/Ausgangskanaele waehlst du oben ueber \"Options\" -> \"Audio/MIDI Settings\"."
                        : "Ein- und Ausgaenge werden in der DAW geroutet.",
                    juce::dontSendNotification);
    ioHint.setFont (juce::Font (juce::FontOptions (12.0f)));
    ioHint.setColour (juce::Label::textColourId, DevTestColours::text.withAlpha (0.7f));
    ioHint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (ioHint);

    // ---------------- Parameter verbinden ----------------
    auto& s = processorRef.apvts;
    srcLAttach    = std::make_unique<ComboAttachment>  (s, ParamIDs::scopeSourceL, srcLBox);
    srcRAttach    = std::make_unique<ComboAttachment>  (s, ParamIDs::scopeSourceR, srcRBox);
    genOutAttach  = std::make_unique<ComboAttachment>  (s, ParamIDs::genOutput,    genOutBox);
    timeAttach    = std::make_unique<SliderAttachment> (s, ParamIDs::timePerDiv,   timeSlider);
    freqAttach    = std::make_unique<SliderAttachment> (s, ParamIDs::genFreq,      freqSlider);
    levelAttach   = std::make_unique<SliderAttachment> (s, ParamIDs::genLevel,     levelSlider);
    triggerAttach = std::make_unique<ButtonAttachment> (s, ParamIDs::trigger,      triggerButton);
    passAttach    = std::make_unique<ButtonAttachment> (s, ParamIDs::passThrough,  passThroughButton);
    genOnAttach   = std::make_unique<ButtonAttachment> (s, ParamIDs::genOn,        genOnButton);

    setResizable (true, true);
    setResizeLimits (820, 520, 2400, 1600);
    setSize (1040, 640);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::setupLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (14.0f)));
    addAndMakeVisible (label);
}

void AudioPluginAudioProcessorEditor::setupSlider (juce::Slider& slider)
{
    addAndMakeVisible (slider);
    slider.setColour (juce::Slider::textBoxTextColourId,       DevTestColours::text);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, DevTestColours::background);
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 96, 24);
    slider.setTextBoxIsEditable (true); // Wert kann direkt eingetippt werden, z. B. "440" oder "1k"
}

void AudioPluginAudioProcessorEditor::setupCombo (juce::ComboBox& box, const juce::String& paramID)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter (paramID)))
        box.addItemList (choice->choices, 1);

    addAndMakeVisible (box);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace DevTestColours;

    g.fillAll (background);

    // Kopfzeile
    auto header = getLocalBounds().removeFromTop (44).reduced (16, 0);

    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("DevTest", header, juce::Justification::centredLeft);

    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("Oszilloskop & Signalgenerator", header.withTrimmedLeft (100),
                juce::Justification::centredLeft);

    g.setColour (text.withAlpha (0.2f));
    g.drawHorizontalLine (44, 0.0f, (float) getWidth());
}

void AudioPluginAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (44);
    area.reduce (12, 12);

    // Rechte Spalte mit Bedienelementen
    auto panel = area.removeFromRight (310);
    area.removeFromRight (12);

    // Links: Oszilloskop + Zeit-Regler darunter
    auto timeRow = area.removeFromBottom (32);
    area.removeFromBottom (8);
    scope.setBounds (area);

    timeLabel.setBounds (timeRow.removeFromLeft (90));
    timeSlider.setBounds (timeRow);

    // ---------------- Gruppe Oszilloskop ----------------
    constexpr int rowH = 30, gap = 6, labelW = 90;

    auto scopeArea = panel.removeFromTop (28 + 4 * (rowH + gap) + 8);
    scopeGroup.setBounds (scopeArea);
    scopeArea = scopeArea.reduced (12).withTrimmedTop (14);

    auto row = scopeArea.removeFromTop (rowH);
    srcLLabel.setBounds (row.removeFromLeft (labelW));
    srcLBox.setBounds (row);
    scopeArea.removeFromTop (gap);

    row = scopeArea.removeFromTop (rowH);
    srcRLabel.setBounds (row.removeFromLeft (labelW));
    srcRBox.setBounds (row);
    scopeArea.removeFromTop (gap);

    triggerButton.setBounds (scopeArea.removeFromTop (rowH));
    scopeArea.removeFromTop (gap);
    passThroughButton.setBounds (scopeArea.removeFromTop (rowH));

    panel.removeFromTop (12);

    // ---------------- Gruppe Generator ----------------
    auto genArea = panel.removeFromTop (28 + 4 * (rowH + gap) + 8);
    genGroup.setBounds (genArea);
    genArea = genArea.reduced (12).withTrimmedTop (14);

    genOnButton.setBounds (genArea.removeFromTop (rowH));
    genArea.removeFromTop (gap);

    row = genArea.removeFromTop (rowH);
    freqLabel.setBounds (row.removeFromLeft (labelW));
    freqSlider.setBounds (row);
    genArea.removeFromTop (gap);

    row = genArea.removeFromTop (rowH);
    levelLabel.setBounds (row.removeFromLeft (labelW));
    levelSlider.setBounds (row);
    genArea.removeFromTop (gap);

    row = genArea.removeFromTop (rowH);
    genOutLabel.setBounds (row.removeFromLeft (labelW));
    genOutBox.setBounds (row);

    panel.removeFromTop (12);
    ioHint.setBounds (panel.removeFromTop (60));
}
