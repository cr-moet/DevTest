#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "CalibrationComponent.h"
#include "DevTestDialog.h"

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

    setupGroup (*this, timeGroup, "Timebase / Trigger");

    timeAttach    = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::timePerDiv, timeSlider);
    triggerAttach = std::make_unique<ButtonAttachment> (p.apvts, ParamIDs::trigger,    triggerButton);
}

void ScopePage::resized()
{
    auto area = getLocalBounds();
    auto block = area.removeFromBottom (groupHeight (1));
    area.removeFromBottom (8);
    scope.setBounds (area);

    timeGroup.setBounds (block);
    auto row = groupContent (block);
    timeLabel.setBounds (row.removeFromLeft (90));
    triggerButton.setBounds (row.removeFromRight (220));
    row.removeFromRight (16);
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

    setupGroup (*this, analysisGroup, "FFT Analysis");
    setupGroup (*this, displayGroup,  "Display");

    sizeAttach   = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftSize,    sizeBox);
    windowAttach = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftWindow,  windowBox);
    avgAttach    = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftAverage, avgBox);
    rangeAttach  = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftRange,   rangeBox);
    maxFreqAttach = std::make_unique<ComboAttachment> (p.apvts, ParamIDs::fftMaxFreq, maxFreqBox);
}

void FftPage::resized()
{
    auto area = getLocalBounds();
    auto blocks = area.removeFromBottom (groupHeight (2));
    area.removeFromBottom (8);
    spectrum.setBounds (area);

    // Left block: FFT analysis settings, right block: display settings
    auto analysisArea = blocks.removeFromLeft (blocks.getWidth() * 3 / 5);
    blocks.removeFromLeft (10);
    analysisGroup.setBounds (analysisArea);
    displayGroup.setBounds (blocks);

    const auto place = [] (juce::Rectangle<int> cell, juce::Label& label, juce::ComboBox& box)
    {
        label.setBounds (cell.removeFromLeft (82));
        box.setBounds (cell);
    };

    auto a = groupContent (analysisArea);
    auto aRow1 = a.removeFromTop (32);
    a.removeFromTop (6);
    auto aRow2 = a.removeFromTop (32);
    place (aRow1.removeFromLeft (aRow1.getWidth() / 2).withTrimmedRight (12), sizeLabel, sizeBox);
    place (aRow1, windowLabel, windowBox);
    place (aRow2.removeFromLeft (aRow2.getWidth() / 2).withTrimmedRight (12), avgLabel, avgBox);

    auto d = groupContent (blocks);
    place (d.removeFromTop (32), rangeLabel, rangeBox);
    d.removeFromTop (6);
    place (d.removeFromTop (32), maxFreqLabel, maxFreqBox);
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

    setupGroup (*this, signalGroup, "Test Signal");

    waveAttach     = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::kneeWave,     waveBox);
    outputAttach   = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::kneeOutput,   outputBox);
    outputBox.onChange = [this] { knee.repaint(); };   // input axis uses the calibration of this output
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

    // Right column: START and the test signal block.
    // The knee display uses the full height to the left of it.
    auto column = area.removeFromRight (210);
    area.removeFromRight (12);
    knee.setBounds (area);

    startButton.setBounds (column.removeFromTop (36));
    column.removeFromTop (14);

    auto block = column.removeFromTop (26 + 4 * (22 + 28) + 3 * 10 + 10);
    signalGroup.setBounds (block);
    auto inner = groupContent (block);

    for (auto [label, control] : { std::pair<juce::Label*, juce::Component*> { &waveLabel, &waveBox },
                                   { &freqLabel, &freqSlider }, { &durationLabel, &durationSlider },
                                   { &outputLabel, &outputBox } })
    {
        label->setBounds (inner.removeFromTop (22));
        control->setBounds (inner.removeFromTop (28));
        inner.removeFromTop (10);
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

    setupGroup (*this, signalGroup,   "Test Signal");
    setupGroup (*this, analysisGroup, "Analysis");

    modeAttach     = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swMode,      modeBox);
    lawAttach      = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swLaw,       lawBox);
    endAttach      = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swEnd,       endBox);
    outputAttach   = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swOutput,    outputBox);
    rangeAttach    = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swRange,     rangeBox);
    smoothAttach   = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swSmooth,    smoothBox);
    gateAttach     = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::swGate,      gateBox);
    durationAttach = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::swDuration,  durationSlider);
    levelAttach    = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::swLevel,     levelSlider);
    setupLevelText (levelSlider, p, [&p] { return juce::roundToInt (p.apvts.getRawParameterValue (ParamIDs::swOutput)->load()) == 1 ? 1 : 0; });
    outputBox.onChange = [this] { levelSlider.updateText(); };
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

    // Bottom: START + test signal block
    auto bottom = area.removeFromBottom (groupHeight (2));
    area.removeFromBottom (8);

    // Right column: analysis / display block
    auto column = area.removeFromRight (180);
    area.removeFromRight (10);
    sweep.setBounds (area);

    // ---- Analysis block ----
    {
        column.removeFromTop (40);   // level with the plot frame
        auto block = column.removeFromTop (26 + 3 * (22 + 28) + 3 * 8 + 2 * 28 + 4 + 10);
        analysisGroup.setBounds (block);
        auto inner = groupContent (block);

        for (auto [label, box] : { std::pair<juce::Label*, juce::ComboBox*> { &rangeLabel, &rangeBox },
                                   { &smoothLabel, &smoothBox }, { &gateLabel, &gateBox } })
        {
            label->setBounds (inner.removeFromTop (22));
            box->setBounds (inner.removeFromTop (28));
            inner.removeFromTop (8);
        }
        normButton.setBounds (inner.removeFromTop (28));
        inner.removeFromTop (4);
        continuousButton.setBounds (inner.removeFromTop (28));

        column.removeFromTop (8);
        noteLabel.setBounds (column.removeFromTop (80));
    }

    // ---- START + test signal block ----
    startButton.setBounds (bottom.removeFromLeft (110).withSizeKeepingCentre (110, 40));
    bottom.removeFromLeft (12);
    signalGroup.setBounds (bottom);
    auto inner = groupContent (bottom);
    auto row1 = inner.removeFromTop (32);
    inner.removeFromTop (6);
    auto row2 = inner.removeFromTop (32);

    // Row 1: mode, law (sweep) or interval (impulse), end frequency
    modeLabel.setBounds (row1.removeFromLeft (46));
    modeBox.setBounds (row1.removeFromLeft (160));
    row1.removeFromLeft (12);
    {
        auto slot = row1.removeFromLeft (180);
        lawLabel.setBounds (slot.removeFromLeft (36));
        lawBox.setBounds (slot);
    }
    {
        auto slot = row1.withX (lawLabel.getX()).withWidth (180);
        intervalLabel.setBounds (slot.removeFromLeft (70));
        intervalBox.setBounds (slot);
    }
    row1.removeFromLeft (12);
    endLabel.setBounds (row1.removeFromLeft (26));
    endBox.setBounds (row1.removeFromLeft (juce::jmin (110, row1.getWidth())));

    // Row 2: duration (sweep) or averages (impulse), level, output
    const int sliderW = juce::jmax (130, (row2.getWidth() - 74 - 54 - 56 - 90 - 24) / 2);
    {
        auto slot = row2.withWidth (74 + sliderW);
        averagesLabel.setBounds (slot.removeFromLeft (74));
        averagesBox.setBounds (slot.removeFromLeft (70));
    }
    durationLabel.setBounds (row2.removeFromLeft (74));
    durationSlider.setBounds (row2.removeFromLeft (sliderW));
    row2.removeFromLeft (12);
    levelLabel.setBounds (row2.removeFromLeft (54));
    levelSlider.setBounds (row2.removeFromLeft (sliderW));
    row2.removeFromLeft (12);
    outputLabel.setBounds (row2.removeFromLeft (56));
    outputBox.setBounds (row2.removeFromLeft (juce::jmin (90, row2.getWidth())));
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
    setupLabel (*this, len1Label,   "Length");
    setupLabel (*this, level1Label, "Level");
    setupLabel (*this, len2Label,   "Length");
    setupLabel (*this, level2Label, "Level");

    setupGroup (*this, signalGroup, "Test Signal");
    setupGroup (*this, burst1Group, "Burst 1");
    setupGroup (*this, burst2Group, "Burst 2  (% of burst 1)");

    for (auto* s : { &freqSlider, &len1Slider, &level1Slider, &len2Slider, &level2Slider })
        setupSlider (*this, *s);
    level2Slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 130, 24);   // "25 % (-12.0 dB)"

    setupCombo (*this, outputBox, p.apvts, ParamIDs::arOutput);

    freqAttach   = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arFreq,    freqSlider);
    len1Attach   = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arLen1,    len1Slider);
    level1Attach = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arLevel1,  level1Slider);
    setupLevelText (level1Slider, p, [&p] { return juce::roundToInt (p.apvts.getRawParameterValue (ParamIDs::arOutput)->load()) == 1 ? 1 : 0; });
    len2Attach   = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arLen2,    len2Slider);
    level2Attach = std::make_unique<SliderAttachment> (p.apvts, ParamIDs::arPercent, level2Slider);
    outputAttach = std::make_unique<ComboAttachment>  (p.apvts, ParamIDs::arOutput,  outputBox);
    outputBox.onChange = [this] { level1Slider.updateText(); display.repaint(); };

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
    auto bottom = area.removeFromBottom (groupHeight (2));
    area.removeFromBottom (8);
    display.setBounds (area);

    // START, then three blocks: test signal, burst 1, burst 2
    startButton.setBounds (bottom.removeFromLeft (110).withSizeKeepingCentre (110, 40));
    bottom.removeFromLeft (12);

    const int blockW = (bottom.getWidth() - 20) / 3;
    auto signalArea = bottom.removeFromLeft (blockW);
    bottom.removeFromLeft (10);
    auto burst1Area = bottom.removeFromLeft (blockW);
    bottom.removeFromLeft (10);
    auto burst2Area = bottom;

    signalGroup.setBounds (signalArea);
    burst1Group.setBounds (burst1Area);
    burst2Group.setBounds (burst2Area);

    // Two rows per block: label left, control right
    const auto twoRows = [] (juce::Rectangle<int> block, juce::Label& l1, juce::Component& c1,
                             juce::Label& l2, juce::Component& c2)
    {
        auto inner = groupContent (block);
        auto row1 = inner.removeFromTop (32);
        inner.removeFromTop (6);
        auto row2 = inner.removeFromTop (32);
        l1.setBounds (row1.removeFromLeft (78));
        c1.setBounds (row1);
        l2.setBounds (row2.removeFromLeft (78));
        c2.setBounds (row2);
    };

    twoRows (signalArea, freqLabel, freqSlider, outputLabel, outputBox);
    twoRows (burst1Area, len1Label, len1Slider, level1Label, level1Slider);
    twoRows (burst2Area, len2Label, len2Slider, level2Label, level2Slider);
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
        // Hardware-style buttons with LED: the LED of the active tab lights up
        tab->getProperties().set ("led", true);
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
    setupLevelText (levelSlider, p, [&p] { return juce::roundToInt (p.apvts.getRawParameterValue (ParamIDs::genOutput)->load()) == 2 ? 1 : 0; });
    genOutBox.onChange = [this] { levelSlider.updateText(); };

    // ---------------- Level unit switch ----------------
    setupLabel (*this, unitLabel, "Unit");
    unitLabel.setJustificationType (juce::Justification::centredRight);
    for (auto* b : { &unitFsButton, &unitDbuButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1002);
        b->getProperties().set ("led", true);
        b->getProperties().set ("keepCase", true);
        addAndMakeVisible (b);
    }
    (p.isUnitDbu() ? unitDbuButton : unitFsButton).setToggleState (true, juce::dontSendNotification);
    // ---------------- Input: sidechain (plugin only) ----------------
    if (p.hasSidechain())
    {
        setupLabel (*this, inputLabel, "Input");
        inputLabel.setJustificationType (juce::Justification::centredRight);
        sidechainLed.getProperties().set ("led", true);
        sidechainLed.setInterceptsMouseClicks (false, false);   // display only
        sidechainLed.setTooltip ("All measurements use the sidechain input");
        addAndMakeVisible (sidechainLed);
        sidechainTimer.startTimerHz (4);
    }

    unitFsButton.onClick  = [this] { if (unitFsButton.getToggleState())  { processorRef.setUnitDbu (false); refreshUnits(); } };
    unitDbuButton.onClick = [this] { if (unitDbuButton.getToggleState()) { processorRef.setUnitDbu (true);  refreshUnits(); } };

    // ---------------- Options menu; window buttons (standalone only) ----------------
    optionsButton.onClick = [this] { showOptionsMenu(); };
    addAndMakeVisible (optionsButton);
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
    if (calibrationWindow != nullptr)
        delete calibrationWindow.getComponent();

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
                                                         std::function<void()> onAudioSettings)
{
    closeButton.onClick    = std::move (onClose);
    minimiseButton.onClick = std::move (onMinimise);
    showAudioSettings      = std::move (onAudioSettings);

    hasWindowControls = true;
    closeButton.setVisible (true);
    minimiseButton.setVisible (true);
    resized();
}

void AudioPluginAudioProcessorEditor::showOptionsMenu()
{
    juce::PopupMenu menu;
    if (showAudioSettings != nullptr)
        menu.addItem ("Audio settings...", [this] { showAudioSettings(); });
    menu.addItem ("Calibration...", [this] { showCalibration(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton));
}

void AudioPluginAudioProcessorEditor::showCalibration()
{
    if (calibrationWindow != nullptr)
    {
        calibrationWindow->toFront (true);
        return;
    }

    calibrationWindow = DevTestDialog::show ("Calibration",
                                             std::make_unique<CalibrationComponent> (processorRef, [this] { refreshUnits(); }),
                                             this);
}

void AudioPluginAudioProcessorEditor::refreshUnits()
{
    levelSlider.updateText();
    kneePage.refreshUnits();
    sweepPage.refreshUnits();
    attRelPage.refreshUnits();
    repaint();
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

// Standalone: dragging the header moves the borderless window
void AudioPluginAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    draggingWindow = false;

    if (! isCaptionArea (e.getPosition()))
        return;

    if (auto* window = getTopLevelComponent())
    {
        draggingWindow     = true;
        dragStartWindowPos = window->getScreenPosition();
        dragStartMouse     = juce::Desktop::getMousePosition();
        dragTimer.startTimerHz (120);   // keeps following even if mouse events get lost
    }
}

void AudioPluginAudioProcessorEditor::mouseDrag (const juce::MouseEvent&)
{
    updateWindowDrag();
}

void AudioPluginAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    updateWindowDrag();
    draggingWindow = false;
    dragTimer.stopTimer();
}

void AudioPluginAudioProcessorEditor::updateWindowDrag()
{
    if (! draggingWindow)
        return;

    // Mouse button released (also outside the window): stop
    if (! juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown())
    {
        draggingWindow = false;
        dragTimer.stopTimer();
        return;
    }

    if (auto* window = getTopLevelComponent())
    {
        const auto target = dragStartWindowPos + (juce::Desktop::getMousePosition() - dragStartMouse);
        if (window->getScreenPosition() != target)
            window->setTopLeftPosition (target);
    }
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
    glyphs.addLineOfText (juce::Font (juce::FontOptions (23.0f, juce::Font::bold)), "DevTest", 0.0f, 0.0f);

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

    // Program name: filled box, the lettering is cut out (background colour + noise)
    g.setColour (text);
    g.fillRect (titleBox);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (titlePath);
        g.setColour (background);
        g.fillRect (titleBox);
        NoiseTexture::draw (g, titleBox.toNearestInt());
    }

    g.setColour (text.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("MEASUREMENT & TEST TOOL",
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
        // (in the plugin only the options button, at the right edge)
        auto buttons = getLocalBounds().removeFromTop (16 + 6).withTrimmedTop (6).withTrimmedRight (6);
        if (hasWindowControls)
        {
            closeButton.setBounds    (buttons.removeFromRight (16));
            buttons.removeFromRight (4);
            minimiseButton.setBounds (buttons.removeFromRight (16));
            buttons.removeFromRight (4);
        }
        optionsButton.setBounds  (buttons.removeFromRight (16));
    }

    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);
    area.reduce (12, 10);

    // Unit switch: top right, in the row of the tabs (above the generator on Scope/FFT)
    {
        auto unitRow = area.withHeight (30);
        unitDbuButton.setBounds (unitRow.removeFromRight (64));
        unitFsButton.setBounds  (unitRow.removeFromRight (64));
        unitLabel.setBounds     (unitRow.removeFromRight (56));

        // Sidechain indicator left of it (plugin only)
        unitRow.removeFromRight (18);
        sidechainLed.setBounds (unitRow.removeFromRight (96));
        inputLabel.setBounds   (unitRow.removeFromRight (60));
    }

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
    genArea = genArea.reduced (12).withTrimmedTop (18);

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
