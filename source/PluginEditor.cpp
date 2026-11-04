#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace UiHelpers;

//==============================================================================
// ScopePage
//==============================================================================
ScopePage::ScopePage (AudioPluginAudioProcessor& p)
    : scope (p)
{
    addAndMakeVisible (scope);
    setupLabel  (*this, timeLabel, "Time / Div");
    setupSlider (*this, timeSlider);
    addAndMakeVisible (triggerButton);

    timeAttach    = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::timePerDiv, timeSlider);
    triggerAttach = std::make_unique<ButtonAttachment> (p.apvts, ParamIDs::trigger,    triggerButton);
}

void ScopePage::resized()
{
    auto area = getLocalBounds();
    auto row  = area.removeFromBottom (32);
    area.removeFromBottom (8);
    scope.setBounds (area);

    timeLabel.setBounds (row.removeFromLeft (90));
    triggerButton.setBounds (row.removeFromRight (200));
    row.removeFromRight (12);
    timeSlider.setBounds (row);
}

//==============================================================================
// FftPage
//==============================================================================
FftPage::FftPage (AudioPluginAudioProcessor& p)
    : spectrum (p)
{
    addAndMakeVisible (spectrum);

    setupLabel (*this, sizeLabel,   "Size");
    setupLabel (*this, windowLabel, "Window");
    setupLabel (*this, avgLabel,    "Averaging");
    setupLabel (*this, rangeLabel,  "Range");

    setupCombo (*this, sizeBox,   p.apvts, ParamIDs::fftSize);
    setupCombo (*this, windowBox, p.apvts, ParamIDs::fftWindow);
    setupCombo (*this, avgBox,    p.apvts, ParamIDs::fftAverage);
    setupCombo (*this, rangeBox,  p.apvts, ParamIDs::fftRange);

    sizeAttach   = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftSize,    sizeBox);
    windowAttach = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftWindow,  windowBox);
    avgAttach    = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftAverage, avgBox);
    rangeAttach  = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftRange,   rangeBox);
}

void FftPage::resized()
{
    auto area = getLocalBounds();
    auto row  = area.removeFromBottom (32);
    area.removeFromBottom (8);
    spectrum.setBounds (area);

    // Four settings side by side, equal width
    const int cell = row.getWidth() / 4;
    const std::pair<juce::Label*, juce::ComboBox*> items[] = {
        { &sizeLabel, &sizeBox }, { &windowLabel, &windowBox }, { &avgLabel, &avgBox }, { &rangeLabel, &rangeBox } };

    for (auto [label, box] : items)
    {
        auto c = row.removeFromLeft (cell).withTrimmedRight (10);
        label->setBounds (c.removeFromLeft (juce::jmin (78, c.getWidth() / 2)));
        box->setBounds (c);
    }
}

//==============================================================================
// Editor
//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), scopePage (p), fftPage (p)
{
    setLookAndFeel (&lookAndFeel);

    // ---------------- Tabs ----------------
    for (auto* tab : { &scopeTab, &fftTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (1001);
        tab->setColour (juce::TextButton::buttonColourId,   DevTestColours::background);
        tab->setColour (juce::TextButton::buttonOnColourId, DevTestColours::accent);
        tab->setColour (juce::TextButton::textColourOffId,  DevTestColours::text);
        tab->setColour (juce::TextButton::textColourOnId,   DevTestColours::background);
        tab->setConnectedEdges (juce::Button::ConnectedOnBottom);
        addAndMakeVisible (tab);
    }
    scopeTab.onClick = [this] { if (scopeTab.getToggleState()) showPage (0); };
    fftTab.onClick   = [this] { if (fftTab.getToggleState())   showPage (1); };

    addChildComponent (scopePage);
    addChildComponent (fftPage);

    // ---------------- Channels ----------------
    auto& s = processorRef.apvts;

    addAndMakeVisible (channelGroup);
    setupLabel (*this, srcLLabel, "Channel L");
    setupLabel (*this, srcRLabel, "Channel R");
    setupCombo (*this, srcLBox, s, ParamIDs::scopeSourceL);
    setupCombo (*this, srcRBox, s, ParamIDs::scopeSourceR);
    addAndMakeVisible (passThroughButton);

    // ---------------- Generator ----------------
    addAndMakeVisible (genGroup);
    addAndMakeVisible (genOnButton);
    setupLabel (*this, waveLabel,   "Waveform");
    setupLabel (*this, freqLabel,   "Frequency");
    setupLabel (*this, levelLabel,  "Amplitude");
    setupLabel (*this, genOutLabel, "Output");
    setupCombo (*this, waveBox,   s, ParamIDs::genWave);
    setupCombo (*this, genOutBox, s, ParamIDs::genOutput);
    setupSlider (*this, freqSlider);
    setupSlider (*this, levelSlider);

    // ---------------- Input/output hint ----------------
    const bool isStandalone = processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    ioHint.setText (isStandalone
                        ? "Select the audio interface (ASIO) and input/output channels via \"Options\" -> \"Audio/MIDI Settings\" at the top."
                        : "Inputs and outputs are routed in the DAW.",
                    juce::dontSendNotification);
    ioHint.setFont (juce::Font (juce::FontOptions (12.0f)));
    ioHint.setColour (juce::Label::textColourId, DevTestColours::text.withAlpha (0.7f));
    ioHint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (ioHint);

    // ---------------- Connect parameters ----------------
    srcLAttach   = std::make_unique<ComboAttachment>  (s, ParamIDs::scopeSourceL, srcLBox);
    srcRAttach   = std::make_unique<ComboAttachment>  (s, ParamIDs::scopeSourceR, srcRBox);
    genOutAttach = std::make_unique<ComboAttachment>  (s, ParamIDs::genOutput,    genOutBox);
    waveAttach   = std::make_unique<ComboAttachment>  (s, ParamIDs::genWave,      waveBox);
    freqAttach   = std::make_unique<SliderAttachment> (s, ParamIDs::genFreq,      freqSlider);
    levelAttach  = std::make_unique<SliderAttachment> (s, ParamIDs::genLevel,     levelSlider);
    passAttach   = std::make_unique<ButtonAttachment> (s, ParamIDs::passThrough,  passThroughButton);
    genOnAttach  = std::make_unique<ButtonAttachment> (s, ParamIDs::genOn,        genOnButton);

    // Restore the last selected tab (saved with the project)
    const int lastPage = (int) processorRef.apvts.state.getProperty ("page", 0);
    (lastPage == 1 ? fftTab : scopeTab).setToggleState (true, juce::dontSendNotification);
    showPage (lastPage == 1 ? 1 : 0);

    setResizable (true, true);
    setResizeLimits (860, 560, 2400, 1600);
    setSize (1060, 680);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void AudioPluginAudioProcessorEditor::showPage (int index)
{
    scopePage.setVisible (index == 0);
    fftPage.setVisible   (index == 1);
    processorRef.apvts.state.setProperty ("page", index, nullptr);
}

//==============================================================================
// Places the program name in a box with the same spacing on all four sides.
// The text is converted to a path so its exact outline (cap height to baseline)
// can be measured and centred in the box.
void AudioPluginAudioProcessorEditor::layoutTitle()
{
    constexpr float border  = titleBorder;
    constexpr float padding = 6.0f;    // gap between text and frame, equal on all sides

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)), "DevTest", 0.0f, 0.0f);

    titlePath.clear();
    glyphs.createPath (titlePath);
    const auto textBounds = titlePath.getBounds();

    const float boxW = std::round (textBounds.getWidth()  + 2.0f * (padding + border));
    const float boxH = std::round (textBounds.getHeight() + 2.0f * (padding + border));
    titleBox = { 14.0f, std::round (((float) headerHeight - boxH) * 0.5f), boxW, boxH };

    // Move the text so its outline sits exactly in the centre of the box
    titlePath.applyTransform (juce::AffineTransform::translation (titleBox.getCentreX() - textBounds.getCentreX(),
                                                                  titleBox.getCentreY() - textBounds.getCentreY()));
}

void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace DevTestColours;

    // Background colour + noise texture on top
    g.fillAll (background);
    NoiseTexture::draw (g, getLocalBounds());

    // Program name in a framed box
    g.setColour (text);
    g.fillPath (titlePath);
    g.drawRect (titleBox, titleBorder);

    g.setColour (text.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("Measurement & Test Tool",
                juce::Rectangle<float> (titleBox.getRight() + 14.0f, titleBox.getY(), 300.0f, titleBox.getHeight()),
                juce::Justification::centredLeft);

    g.setColour (text.withAlpha (0.2f));
    g.drawHorizontalLine (headerHeight, 0.0f, (float) getWidth());

    // Line below the tabs
    g.setColour (accent);
    g.fillRect (scopeTab.getX(), scopeTab.getBottom(), fftTab.getRight() - scopeTab.getX(), 2);
}

void AudioPluginAudioProcessorEditor::resized()
{
    layoutTitle();

    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);
    area.reduce (12, 10);

    // Right column with controls
    auto panel = area.removeFromRight (310);
    area.removeFromRight (12);

    // Tabs above the display
    auto tabRow = area.removeFromTop (30);
    scopeTab.setBounds (tabRow.removeFromLeft (90));
    fftTab.setBounds   (tabRow.removeFromLeft (90));
    area.removeFromTop (8);

    scopePage.setBounds (area);
    fftPage.setBounds   (area);

    constexpr int rowH = 30, gap = 6, labelW = 92;

    // ---------------- Channels group ----------------
    auto chArea = panel.removeFromTop (28 + 3 * (rowH + gap) + 8);
    channelGroup.setBounds (chArea);
    chArea = chArea.reduced (12).withTrimmedTop (14);

    auto row = chArea.removeFromTop (rowH);
    srcLLabel.setBounds (row.removeFromLeft (labelW));
    srcLBox.setBounds (row);
    chArea.removeFromTop (gap);

    row = chArea.removeFromTop (rowH);
    srcRLabel.setBounds (row.removeFromLeft (labelW));
    srcRBox.setBounds (row);
    chArea.removeFromTop (gap);

    passThroughButton.setBounds (chArea.removeFromTop (rowH));

    panel.removeFromTop (12);

    // ---------------- Generator group ----------------
    auto genArea = panel.removeFromTop (28 + 5 * (rowH + gap) + 8);
    genGroup.setBounds (genArea);
    genArea = genArea.reduced (12).withTrimmedTop (14);

    genOnButton.setBounds (genArea.removeFromTop (rowH));
    genArea.removeFromTop (gap);

    const std::pair<juce::Label*, juce::Component*> rows[] = {
        { &waveLabel, &waveBox }, { &freqLabel, &freqSlider }, { &levelLabel, &levelSlider }, { &genOutLabel, &genOutBox } };

    for (auto [label, comp] : rows)
    {
        row = genArea.removeFromTop (rowH);
        label->setBounds (row.removeFromLeft (labelW));
        comp->setBounds (row);
        genArea.removeFromTop (gap);
    }

    panel.removeFromTop (12);
    ioHint.setBounds (panel.removeFromTop (60));
}
