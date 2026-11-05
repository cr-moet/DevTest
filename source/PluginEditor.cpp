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
    setupLabel (*this, maxFreqLabel, "Max freq");

    setupCombo (*this, sizeBox,   p.apvts, ParamIDs::fftSize);
    setupCombo (*this, windowBox, p.apvts, ParamIDs::fftWindow);
    setupCombo (*this, avgBox,    p.apvts, ParamIDs::fftAverage);
    setupCombo (*this, rangeBox,  p.apvts, ParamIDs::fftRange);
    setupCombo (*this, maxFreqBox, p.apvts, ParamIDs::fftMaxFreq);

    sizeAttach   = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftSize,    sizeBox);
    windowAttach = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftWindow,  windowBox);
    avgAttach    = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftAverage, avgBox);
    rangeAttach  = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftRange,   rangeBox);
    maxFreqAttach = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftMaxFreq, maxFreqBox);
}

void FftPage::resized()
{
    auto area = getLocalBounds();
    auto row2 = area.removeFromBottom (32);
    area.removeFromBottom (6);
    auto row1 = area.removeFromBottom (32);
    area.removeFromBottom (8);
    spectrum.setBounds (area);

    // Settings in two rows: three cells on top, two below (aligned with the top cells)
    const int cell = row1.getWidth() / 3;

    const auto place = [cell] (juce::Rectangle<int>& row, juce::Label& label, juce::ComboBox& box)
    {
        auto c = row.removeFromLeft (cell).withTrimmedRight (12);
        label.setBounds (c.removeFromLeft (80));
        box.setBounds (c);
    };

    place (row1, sizeLabel,    sizeBox);
    place (row1, windowLabel,  windowBox);
    place (row1, avgLabel,     avgBox);
    place (row2, rangeLabel,   rangeBox);
    place (row2, maxFreqLabel, maxFreqBox);
}

//==============================================================================
// KneePage
//==============================================================================
KneePage::KneePage (AudioPluginAudioProcessor& p)
    : processor (p), knee (p)
{
    addAndMakeVisible (knee);

    startButton.setColour (juce::TextButton::buttonColourId,  DevTestColours::accent);
    startButton.setColour (juce::TextButton::textColourOffId, DevTestColours::background);
    startButton.onClick = [this]
    {
        if (processor.isKneeTestRunning())
        {
            processor.stopKneeTest();
        }
        else
        {
            // Switch the normal generator off, so it does not come back after the test
            if (auto* genOn = processor.apvts.getParameter (ParamIDs::genOn))
                genOn->setValueNotifyingHost (0.0f);

            knee.clear();
            processor.startKneeTest();
        }
        timerCallback();
    };
    addAndMakeVisible (startButton);

    setupLabel (*this, waveLabel,     "Waveform");
    setupLabel (*this, freqLabel,     "Frequency");
    setupLabel (*this, durationLabel, "Sweep time");
    setupLabel (*this, outputLabel,   "Output");

    setupCombo (*this, waveBox,   p.apvts, ParamIDs::kneeWave);
    setupCombo (*this, outputBox, p.apvts, ParamIDs::kneeOutput);
    setupSlider (*this, freqSlider);
    setupSlider (*this, durationSlider);

    waveAttach     = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::kneeWave,     waveBox);
    outputAttach   = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::kneeOutput,   outputBox);
    freqAttach     = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::kneeFreq,     freqSlider);
    durationAttach = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::kneeDuration, durationSlider);

    startTimerHz (10);
}

void KneePage::timerCallback()
{
    // Button shows the action it will perform
    const bool running = processor.isKneeTestRunning();
    startButton.setButtonText (running ? "STOP" : "START");

    // Settings cannot be changed during a sweep
    for (juce::Component* c : { (juce::Component*) &waveBox, (juce::Component*) &freqSlider,
                                (juce::Component*) &durationSlider, (juce::Component*) &outputBox })
        c->setEnabled (! running);
}

void KneePage::resized()
{
    auto area = getLocalBounds();

    // Right column: START and the settings, stacked (label above each control).
    // The knee display uses the full height to the left of it.
    auto column = area.removeFromRight (200);
    area.removeFromRight (12);
    knee.setBounds (area);

    startButton.setBounds (column.removeFromTop (36));
    column.removeFromTop (16);

    for (auto [label, control] : { std::pair<juce::Label*, juce::Component*> { &waveLabel, &waveBox },
                                   { &freqLabel, &freqSlider }, { &durationLabel, &durationSlider },
                                   { &outputLabel, &outputBox } })
    {
        label->setBounds (column.removeFromTop (22));
        control->setBounds (column.removeFromTop (28));
        column.removeFromTop (10);
    }
}

//==============================================================================
// SweepPage
//==============================================================================
SweepPage::SweepPage (AudioPluginAudioProcessor& p)
    : processor (p), sweep (p)
{
    addAndMakeVisible (sweep);

    startButton.setColour (juce::TextButton::buttonColourId,  DevTestColours::accent);
    startButton.setColour (juce::TextButton::textColourOffId, DevTestColours::background);
    startButton.onClick = [this]
    {
        if (processor.isSweepRunning())
        {
            processor.stopSweepTest();
        }
        else
        {
            // Switch the normal generator off, so it does not come back after the test
            if (auto* genOn = processor.apvts.getParameter (ParamIDs::genOn))
                genOn->setValueNotifyingHost (0.0f);

            sweep.clear();
            processor.startSweepTest();
        }
        timerCallback();
    };
    addAndMakeVisible (startButton);

    setupLabel (*this, modeLabel,     "Mode");
    setupLabel (*this, lawLabel,      "Law");
    setupLabel (*this, endLabel,      "To");
    setupLabel (*this, durationLabel, "Duration");
    setupLabel (*this, levelLabel,    "Level");
    setupLabel (*this, outputLabel,   "Output");
    setupLabel (*this, rangeLabel,    "Range");
    setupLabel (*this, smoothLabel,   "Smoothing");
    setupLabel (*this, gateLabel,     "IR window");
    setupLabel (*this, intervalLabel, "Interval");
    setupLabel (*this, averagesLabel, "Averages");

    setupCombo (*this, modeBox,   p.apvts, ParamIDs::swMode);
    setupCombo (*this, lawBox,    p.apvts, ParamIDs::swLaw);
    setupCombo (*this, endBox,    p.apvts, ParamIDs::swEnd);
    setupCombo (*this, outputBox, p.apvts, ParamIDs::swOutput);
    setupCombo (*this, rangeBox,  p.apvts, ParamIDs::swRange);
    setupCombo (*this, smoothBox, p.apvts, ParamIDs::swSmooth);
    setupCombo (*this, gateBox,   p.apvts, ParamIDs::swGate);
    setupCombo (*this, intervalBox, p.apvts, ParamIDs::swInterval);
    setupCombo (*this, averagesBox, p.apvts, ParamIDs::swAverages);
    addAndMakeVisible (continuousButton);
    setupSlider (*this, durationSlider);
    setupSlider (*this, levelSlider);
    addAndMakeVisible (normButton);

    noteLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    noteLabel.setColour (juce::Label::textColourId, DevTestColours::accent);
    noteLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (noteLabel);

    modeAttach     = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swMode,      modeBox);
    lawAttach      = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swLaw,       lawBox);
    endAttach      = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swEnd,       endBox);
    outputAttach   = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swOutput,    outputBox);
    rangeAttach    = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swRange,     rangeBox);
    smoothAttach   = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swSmooth,    smoothBox);
    gateAttach     = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swGate,      gateBox);
    durationAttach = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::swDuration,  durationSlider);
    levelAttach    = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::swLevel,     levelSlider);
    normAttach     = std::make_unique<ButtonAttachment> (p.apvts, ParamIDs::swNormalize, normButton);
    intervalAttach = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swInterval,  intervalBox);
    averagesAttach = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swAverages,  averagesBox);
    continuousAttach = std::make_unique<ButtonAttachment> (p.apvts, ParamIDs::swContinuous, continuousButton);

    startTimerHz (10);
}

void SweepPage::timerCallback()
{
    const bool running = processor.isSweepRunning();
    const bool impulse = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIDs::swMode)->load())
                         == (int) SweepMode::impulse;

    startButton.setButtonText (running ? "STOP" : "START");

    // Settings of the test signal cannot be changed during a run
    // (display settings, averages and "Continuous" can)
    for (juce::Component* c : { (juce::Component*) &modeBox, (juce::Component*) &lawBox, (juce::Component*) &endBox,
                                (juce::Component*) &durationSlider, (juce::Component*) &levelSlider,
                                (juce::Component*) &outputBox, (juce::Component*) &intervalBox })
        c->setEnabled (! running);

    // Sweep mode: law + duration. Impulse mode: interval + averages ("Continuous" applies to both)
    lawLabel.setVisible (! impulse);       lawBox.setVisible (! impulse);
    durationLabel.setVisible (! impulse);  durationSlider.setVisible (! impulse);
    intervalLabel.setVisible (impulse);    intervalBox.setVisible (impulse);
    averagesLabel.setVisible (impulse);    averagesBox.setVisible (impulse);

    // Smoothing and IR window only apply to the impulse mode
    smoothBox.setEnabled (impulse);
    gateBox.setEnabled (impulse);

    noteLabel.setText (impulse ? "A single-sample impulse carries little energy: use a high level (e.g. 0 dBFS) "
                                 "and more averages for a clean result."
                               : juce::String(),
                       juce::dontSendNotification);
}

void SweepPage::resized()
{
    auto area = getLocalBounds();
    auto row2 = area.removeFromBottom (32);
    area.removeFromBottom (6);
    auto row1 = area.removeFromBottom (32);
    area.removeFromBottom (8);

    // Right column: display settings
    auto column = area.removeFromRight (150);
    area.removeFromRight (8);
    sweep.setBounds (area);

    column.removeFromTop (60);
    for (auto [label, box] : { std::pair<juce::Label*, juce::ComboBox*> { &rangeLabel, &rangeBox },
                               { &smoothLabel, &smoothBox }, { &gateLabel, &gateBox } })
    {
        label->setBounds (column.removeFromTop (22));
        box->setBounds (column.removeFromTop (28));
        column.removeFromTop (8);
    }
    normButton.setBounds (column.removeFromTop (28));
    column.removeFromTop (4);
    continuousButton.setBounds (column.removeFromTop (28));
    column.removeFromTop (8);
    noteLabel.setBounds (column.removeFromTop (80));

    // Row 1: start, mode, law, end frequency
    startButton.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (14);
    modeLabel.setBounds (row1.removeFromLeft (46));
    modeBox.setBounds (row1.removeFromLeft (170));
    row1.removeFromLeft (12);
    {
        auto slot = row1.removeFromLeft (166);
        lawLabel.setBounds (slot.removeFromLeft (36));
        lawBox.setBounds (slot);
    }
    {
        auto slot = row1.withX (lawLabel.getX()).withWidth (166);
        intervalLabel.setBounds (slot.removeFromLeft (60));
        intervalBox.setBounds (slot);
    }
    row1.removeFromLeft (12);
    endLabel.setBounds (row1.removeFromLeft (26));
    endBox.setBounds (row1.removeFromLeft (juce::jmin (110, row1.getWidth())));

    // Row 2: duration (sweep) or averages + continuous (impulse), level, output
    const int sliderW = juce::jmax (160, (row2.getWidth() - 70 - 54 - 56 - 100 - 24) / 2);
    {
        auto slot = row2.withWidth (70 + sliderW);
        averagesLabel.setBounds (slot.removeFromLeft (70));
        averagesBox.setBounds (slot.removeFromLeft (70));
    }
    durationLabel.setBounds (row2.removeFromLeft (70));
    durationSlider.setBounds (row2.removeFromLeft (sliderW));
    row2.removeFromLeft (12);
    levelLabel.setBounds (row2.removeFromLeft (54));
    levelSlider.setBounds (row2.removeFromLeft (sliderW));
    row2.removeFromLeft (12);
    outputLabel.setBounds (row2.removeFromLeft (56));
    outputBox.setBounds (row2.removeFromLeft (juce::jmin (100, row2.getWidth())));
}

//==============================================================================
// AttRelPage
//==============================================================================
AttRelPage::AttRelPage (AudioPluginAudioProcessor& p)
    : processor (p), display (p)
{
    addAndMakeVisible (display);

    startButton.setColour (juce::TextButton::buttonColourId,  DevTestColours::accent);
    startButton.setColour (juce::TextButton::textColourOffId, DevTestColours::background);
    startButton.onClick = [this]
    {
        if (processor.isBurstRunning())
        {
            processor.stopBurstTest();
        }
        else
        {
            // Switch the normal generator off, so it does not come back after the test
            if (auto* genOn = processor.apvts.getParameter (ParamIDs::genOn))
                genOn->setValueNotifyingHost (0.0f);

            display.clear();
            processor.startBurstTest();
        }
        timerCallback();
    };
    addAndMakeVisible (startButton);

    setupLabel (*this, freqLabel,   "Frequency");
    setupLabel (*this, outputLabel, "Output");
    setupLabel (*this, burst1Label, "Burst 1");
    setupLabel (*this, burst2Label, "Burst 2");
    setupLabel (*this, len1Label,   "Length");
    setupLabel (*this, level1Label, "Level");
    setupLabel (*this, len2Label,   "Length");
    setupLabel (*this, level2Label, "Level");

    for (auto* l : { &burst1Label, &burst2Label })
    {
        l->setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        l->setColour (juce::Label::textColourId, DevTestColours::accent);
    }

    for (auto* s : { &freqSlider, &len1Slider, &level1Slider, &len2Slider, &level2Slider })
        setupSlider (*this, *s);
    level2Slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 130, 24);   // "25 % (-12.0 dB)"

    setupCombo (*this, outputBox, p.apvts, ParamIDs::arOutput);

    freqAttach   = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arFreq,    freqSlider);
    len1Attach   = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arLen1,    len1Slider);
    level1Attach = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arLevel1,  level1Slider);
    len2Attach   = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arLen2,    len2Slider);
    level2Attach = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arPercent, level2Slider);
    outputAttach = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::arOutput,  outputBox);

    startTimerHz (10);
}

void AttRelPage::timerCallback()
{
    const bool running = processor.isBurstRunning();
    startButton.setButtonText (running ? "STOP" : "START");

    for (juce::Component* c : { (juce::Component*) &freqSlider, (juce::Component*) &len1Slider, (juce::Component*) &level1Slider,
                                (juce::Component*) &len2Slider, (juce::Component*) &level2Slider, (juce::Component*) &outputBox })
        c->setEnabled (! running);
}

void AttRelPage::resized()
{
    auto area = getLocalBounds();
    auto row3 = area.removeFromBottom (32);
    area.removeFromBottom (6);
    auto row2 = area.removeFromBottom (32);
    area.removeFromBottom (6);
    auto row1 = area.removeFromBottom (32);
    area.removeFromBottom (8);
    display.setBounds (area);

    // Row 1: start, frequency, output
    startButton.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (14);
    outputBox.setBounds (row1.removeFromRight (100));
    outputLabel.setBounds (row1.removeFromRight (56));
    row1.removeFromRight (12);
    freqLabel.setBounds (row1.removeFromLeft (76));
    freqSlider.setBounds (row1);

    // Rows 2 and 3: length and level of each burst
    const auto burstRow = [] (juce::Rectangle<int> row, juce::Label& title, juce::Label& lenLabel, juce::Slider& len,
                              juce::Label& levelLabel, juce::Slider& level)
    {
        title.setBounds (row.removeFromLeft (70));
        const int half = row.getWidth() / 2;
        auto left = row.removeFromLeft (half).withTrimmedRight (10);
        lenLabel.setBounds (left.removeFromLeft (52));
        len.setBounds (left);
        levelLabel.setBounds (row.removeFromLeft (46));
        level.setBounds (row);
    };

    burstRow (row2, burst1Label, len1Label, len1Slider, level1Label, level1Slider);
    burstRow (row3, burst2Label, len2Label, len2Slider, level2Label, level2Slider);
}

//==============================================================================
// Editor
//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), scopePage (p), fftPage (p), kneePage (p), sweepPage (p), attRelPage (p)
{
    setLookAndFeel (&lookAndFeel);

    // ---------------- Tabs ----------------
    for (auto* tab : { &scopeTab, &fftTab, &kneeTab, &sweepTab, &attRelTab })
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
    kneeTab.onClick  = [this] { if (kneeTab.getToggleState())  showPage (2); };
    sweepTab.onClick = [this] { if (sweepTab.getToggleState()) showPage (3); };
    attRelTab.onClick = [this] { if (attRelTab.getToggleState()) showPage (4); };

    addChildComponent (scopePage);
    addChildComponent (fftPage);
    addChildComponent (kneePage);
    addChildComponent (sweepPage);
    addChildComponent (attRelPage);

    auto& s = processorRef.apvts;

    // ---------------- Generator (Scope and FFT only) ----------------
    addChildComponent (genGroup);
    addChildComponent (genOnButton);
    setupLabel (*this, waveLabel,   "Waveform");
    setupLabel (*this, freqLabel,   "Frequency");
    setupLabel (*this, levelLabel,  "Amplitude");
    setupLabel (*this, genOutLabel, "Output");
    setupCombo (*this, waveBox,   s, ParamIDs::genWave);
    setupCombo (*this, genOutBox, s, ParamIDs::genOutput);
    setupSlider (*this, freqSlider);
    setupSlider (*this, levelSlider);

    genOutAttach = std::make_unique<ComboAttachment>  (s, ParamIDs::genOutput, genOutBox);
    waveAttach   = std::make_unique<ComboAttachment>  (s, ParamIDs::genWave,   waveBox);
    freqAttach   = std::make_unique<SliderAttachment> (s, ParamIDs::genFreq,   freqSlider);
    levelAttach  = std::make_unique<SliderAttachment> (s, ParamIDs::genLevel,  levelSlider);
    genOnAttach  = std::make_unique<ButtonAttachment> (s, ParamIDs::genOn,     genOnButton);

    // ---------------- Standalone window buttons (hidden in the plugin) ----------------
    addChildComponent (optionsButton);
    addChildComponent (minimiseButton);
    addChildComponent (closeButton);

    // Restore the last selected tab (saved with the project)
    const int lastPage = juce::jlimit (0, 4, (int) processorRef.apvts.state.getProperty ("page", 0));
    juce::TextButton* tabs[] = { &scopeTab, &fftTab, &kneeTab, &sweepTab, &attRelTab };
    tabs[lastPage]->setToggleState (true, juce::dontSendNotification);
    showPage (lastPage);

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
    currentPage = index;
    scopePage.setVisible (index == 0);
    fftPage.setVisible   (index == 1);
    kneePage.setVisible  (index == 2);
    sweepPage.setVisible (index == 3);
    attRelPage.setVisible (index == 4);

    // The generator is only needed with Scope and FFT; the other tabs use the full width
    const bool showGenerator = index <= 1;
    for (juce::Component* c : { (juce::Component*) &genGroup, (juce::Component*) &genOnButton,
                                (juce::Component*) &waveLabel, (juce::Component*) &freqLabel,
                                (juce::Component*) &levelLabel, (juce::Component*) &genOutLabel,
                                (juce::Component*) &waveBox, (juce::Component*) &genOutBox,
                                (juce::Component*) &freqSlider, (juce::Component*) &levelSlider })
        c->setVisible (showGenerator);

    processorRef.apvts.state.setProperty ("page", index, nullptr);
    resized();
}

void AudioPluginAudioProcessorEditor::setWindowControls (std::function<void()> onClose, std::function<void()> onMinimise,
                                                         std::function<void()> onOptions)
{
    closeButton.onClick    = std::move (onClose);
    minimiseButton.onClick = std::move (onMinimise);
    optionsButton.onClick  = std::move (onOptions);

    hasWindowControls = true;
    closeButton.setVisible (true);
    minimiseButton.setVisible (true);
    optionsButton.setVisible (true);
    resized();
}

bool AudioPluginAudioProcessorEditor::isCaptionArea (juce::Point<int> position) const
{
    if (! hasWindowControls || position.y < 0 || position.y >= headerHeight)
        return false;

    for (auto* button : { &optionsButton, &minimiseButton, &closeButton })
        if (button->getBounds().contains (position))
            return false;

    return true;
}

// Standalone: dragging the header moves the borderless window.
// On Windows the system does this (the window reports the header as its title bar,
// see StandaloneApp.cpp), so these mouse events only arrive on other systems.
// There the window follows the mouse in screen coordinates.
void AudioPluginAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    draggingWindow = false;

    if (! isCaptionArea (e.getPosition()))
        return;

    if (auto* window = getTopLevelComponent())
    {
        draggingWindow     = true;
        dragStartWindowPos = window->getScreenPosition();
        dragStartMouse     = e.getScreenPosition();
    }
}

void AudioPluginAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingWindow)
        if (auto* window = getTopLevelComponent())
            window->setTopLeftPosition (dragStartWindowPos + (e.getScreenPosition() - dragStartMouse));
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
    g.fillRect (scopeTab.getX(), scopeTab.getBottom(), attRelTab.getRight() - scopeTab.getX(), 2);
}

void AudioPluginAudioProcessorEditor::resized()
{
    layoutTitle();

    // Window buttons in the top right corner (standalone only)
    {
        auto buttons = getLocalBounds().removeFromTop (16 + 6).withTrimmedTop (6).withTrimmedRight (6);
        closeButton.setBounds    (buttons.removeFromRight (16));
        buttons.removeFromRight (4);
        minimiseButton.setBounds (buttons.removeFromRight (16));
        buttons.removeFromRight (4);
        optionsButton.setBounds  (buttons.removeFromRight (16));
    }

    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);
    area.reduce (12, 10);

    // Right column with the generator (Scope and FFT only)
    const bool showGenerator = currentPage <= 1;
    juce::Rectangle<int> panel;
    if (showGenerator)
    {
        panel = area.removeFromRight (310);
        area.removeFromRight (12);
    }

    // Tabs above the display
    auto tabRow = area.removeFromTop (30);
    scopeTab.setBounds (tabRow.removeFromLeft (90));
    fftTab.setBounds   (tabRow.removeFromLeft (90));
    kneeTab.setBounds  (tabRow.removeFromLeft (90));
    sweepTab.setBounds (tabRow.removeFromLeft (90));
    attRelTab.setBounds (tabRow.removeFromLeft (90));
    area.removeFromTop (8);

    scopePage.setBounds (area);
    fftPage.setBounds   (area);
    kneePage.setBounds  (area);
    sweepPage.setBounds (area);
    attRelPage.setBounds (area);

    if (! showGenerator)
        return;

    constexpr int rowH = 30, gap = 6, labelW = 92;

    // ---------------- Generator group ----------------
    // The outline of the group starts at the top of the display frame (Scope / FFT plot):
    // tabs (30) + gap (8) + space above the plot (24), minus half the group title height
    panel.removeFromTop (30 + 8 + 24 - 7);
    auto genArea = panel.removeFromTop (28 + 5 * (rowH + gap) + 8);
    genGroup.setBounds (genArea);
    genArea = genArea.reduced (12).withTrimmedTop (14);

    genOnButton.setBounds (genArea.removeFromTop (rowH));
    genArea.removeFromTop (gap);

    const std::pair<juce::Label*, juce::Component*> rows[] = {
        { &waveLabel, &waveBox }, { &freqLabel, &freqSlider }, { &levelLabel, &levelSlider }, { &genOutLabel, &genOutBox } };

    for (auto [label, comp] : rows)
    {
        auto row = genArea.removeFromTop (rowH);
        label->setBounds (row.removeFromLeft (labelW));
        comp->setBounds (row);
        genArea.removeFromTop (gap);
    }
}
