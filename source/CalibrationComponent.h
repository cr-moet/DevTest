#pragma once

#include "PluginEditor.h"

//==============================================================================
// Calibration of outputs and inputs (opened from the options menu).
//
// A calibration value is the level in dBu of a sine whose peaks reach 0 dBFS,
// e.g. "+18.00 dBu" for an interface where 0 dBFS = +18 dBu. dBu values are RMS
// levels (0 dBu = 0.775 V RMS, +4 dBu = 1.228 V RMS).
//
// Step 1 - outputs: the test tone (1 kHz) is sent with +4 dBu according to the
//          current output calibration. Measure each output with an AC voltmeter
//          and trim it (arrow buttons or typed value, 1 step = 0.01 dB) until the
//          meter reads 1.228 V RMS. The trim changes the output calibration and
//          is saved, so it is shown again when the window is reopened.
// Step 2 - inputs: connect output L -> input L and output R -> input R. With the
//          tone on, CALIBRATE INPUTS sets the inputs so they show +4.00 dBu.
// The values can also be typed in directly at the bottom.
//==============================================================================
class CalibrationComponent final : public juce::Component,
                                   private juce::Timer
{
public:
    static constexpr float toneDbu    = 4.0f;     // level of the test tone
    static constexpr float stepDb     = 0.01f;    // one trim step
    static constexpr int   maxTrim    = 2000;     // +/- 20 dB

    CalibrationComponent (AudioPluginAudioProcessor& p, std::function<void()> onChange)
        : processor (p), onCalibrationChanged (std::move (onChange))
    {
        using namespace UiHelpers;

        setupGroup (*this, outGroup, "Step 1 - Outputs  (test tone)");
        setupGroup (*this, inGroup,  "Step 2 - Inputs  (loopback)");
        setupGroup (*this, valueGroup, "Calibration values  (level at 0 dBFS)");

        for (auto* hint : { &outHint, &inHint, &statusLabel })
        {
            hint->setFont (juce::Font (juce::FontOptions (12.0f)));
            hint->setColour (juce::Label::textColourId, DevTestColours::text.withAlpha (0.75f));
            hint->setJustificationType (juce::Justification::topLeft);
            hint->setMinimumHorizontalScale (1.0f);   // wrap instead of squeezing
            addAndMakeVisible (*hint);
        }
        outHint.setText ("1 kHz sine with +4 dBu on both outputs. Measure each output with an AC voltmeter and trim it "
                         "until the meter reads 1.228 V RMS.  (arrows: 1 step = 0.01 dB, shift+click = 10 steps)",
                         juce::dontSendNotification);
        inHint.setText ("Connect output L -> input L and output R -> input R, keep the tone on and press "
                        "CALIBRATE INPUTS. Afterwards the inputs show +4.00 dBu.",
                        juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, DevTestColours::accent);

        // ---------------- Step 1: tone + trim per output ----------------
        toneButton.setClickingTogglesState (true);
        toneButton.getProperties().set ("led", true);
        toneButton.onClick = [this] { updateTone(); };
        addAndMakeVisible (toneButton);
        styleInfo (toneInfo);
        addAndMakeVisible (toneInfo);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& t = trims[(size_t) ch];
            UiHelpers::setupLabel (*this, t.label, ch == 0 ? "Output L" : "Output R");

            for (auto* b : { &t.down, &t.up })
            {
                b->setColour (juce::TextButton::buttonColourId, DevTestColours::background);
                addAndMakeVisible (*b);
            }
            t.down.onClick = [this, ch] { stepTrim (ch, -1); };
            t.up.onClick   = [this, ch] { stepTrim (ch, +1); };

            styleDisplay (t.field);
            t.field.setEditable (true);
            t.field.onTextChange = [this, ch] { setTrim (ch, trims[(size_t) ch].field.getText().retainCharacters ("-+0123456789").getIntValue()); };
            addAndMakeVisible (t.field);

            styleInfo (t.info);
            addAndMakeVisible (t.info);

            // Saved trim: the calibration value without trim is the reference
            t.steps = juce::jlimit (-maxTrim, maxTrim, (int) processor.apvts.state.getProperty (trimProperty (ch), 0));
            t.base  = processor.getOutputCalibration (ch) + (float) t.steps * stepDb;
        }

        // ---------------- Step 2: inputs ----------------
        for (auto* b : { &calibrateInButton })
        {
            b->setColour (juce::TextButton::buttonColourId,  DevTestColours::accent);
            b->setColour (juce::TextButton::textColourOffId, DevTestColours::background);
            addAndMakeVisible (*b);
        }
        calibrateInButton.onClick = [this] { calibrateInputs(); };

        for (auto* meter : { &inLMeter, &inRMeter })
        {
            styleInfo (*meter);
            addAndMakeVisible (*meter);
        }

        // ---------------- Values (can be typed in) ----------------
        setupLabel (*this, inLLabel,  "Input L");
        setupLabel (*this, inRLabel,  "Input R");
        setupLabel (*this, outLLabel, "Output L");
        setupLabel (*this, outRLabel, "Output R");

        for (auto* s : { &inLSlider, &inRSlider, &outLSlider, &outRSlider })
        {
            UiHelpers::setupSlider (*this, *s);
            s->setRange (-20.0, 40.0, 0.01);
            s->textFromValueFunction = [] (double v) { return fmt ((float) v) + " dBu"; };
            s->valueFromTextFunction = [] (const juce::String& t) { return parseDb (t); };
        }

        inLSlider.onValueChange  = [this] { processor.setInputCalibration (0, (float) inLSlider.getValue()); changed(); };
        inRSlider.onValueChange  = [this] { processor.setInputCalibration (1, (float) inRSlider.getValue()); changed(); };
        // Typed output value: becomes the new reference for the trim
        outLSlider.onValueChange = [this] { outputValueTyped (0, (float) outLSlider.getValue()); };
        outRSlider.onValueChange = [this] { outputValueTyped (1, (float) outRSlider.getValue()); };

        syncValues();
        updateTrimDisplays();

        setSize (680, 500);
        startTimerHz (10);
        timerCallback();
    }

    ~CalibrationComponent() override
    {
        processor.setCalibrationTone (false, -100.0f, -100.0f);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (DevTestColours::background);
        NoiseTexture::draw (g, getLocalBounds());
    }

    void resized() override
    {
        constexpr int rowH = 32, gap = 6, labelW = 96;
        auto area = getLocalBounds().reduced (14);

        const auto row = [&] (juce::Rectangle<int>& block) { auto r = block.removeFromTop (rowH); block.removeFromTop (gap); return r; };
        const auto content = [] (juce::Rectangle<int> b) { return b.reduced (10).withTrimmedTop (16); };

        // ---------------- Step 1 ----------------
        auto outArea = area.removeFromTop (26 + 34 + 3 * (rowH + gap) + 10);
        outGroup.setBounds (outArea);
        auto out = content (outArea);
        outHint.setBounds (out.removeFromTop (30));
        out.removeFromTop (4);
        {
            auto r = row (out);
            r.removeFromLeft (labelW);
            toneButton.setBounds (r.removeFromLeft (190));
            r.removeFromLeft (14);
            toneInfo.setBounds (r);
        }
        for (auto& t : trims)
        {
            auto r = row (out);
            t.label.setBounds (r.removeFromLeft (labelW));
            t.down.setBounds  (r.removeFromLeft (34).withSizeKeepingCentre (34, 28));
            r.removeFromLeft (4);
            t.field.setBounds (r.removeFromLeft (80).withSizeKeepingCentre (80, 26));
            r.removeFromLeft (4);
            t.up.setBounds    (r.removeFromLeft (34).withSizeKeepingCentre (34, 28));
            r.removeFromLeft (14);
            t.info.setBounds (r);
        }

        area.removeFromTop (10);

        // ---------------- Step 2 ----------------
        auto inArea = area.removeFromTop (26 + 34 + 2 * (rowH + gap) + 10);
        inGroup.setBounds (inArea);
        auto in = content (inArea);
        inHint.setBounds (in.removeFromTop (30));
        in.removeFromTop (4);
        {
            auto r = row (in);
            r.removeFromLeft (labelW);
            calibrateInButton.setBounds (r.removeFromLeft (190));
            r.removeFromLeft (14);
            inLMeter.setBounds (r);
        }
        {
            auto r = row (in);
            r.removeFromLeft (labelW + 190 + 14);
            inRMeter.setBounds (r);
        }

        area.removeFromTop (10);

        // ---------------- Values ----------------
        auto valArea = area.removeFromTop (26 + 2 * rowH + gap + 10);
        valueGroup.setBounds (valArea);
        auto val = content (valArea);
        const int colW = (val.getWidth() - 20) / 2;
        auto left  = val.removeFromLeft (colW);
        val.removeFromLeft (20);
        auto right = val;

        const auto valueRow = [&] (juce::Rectangle<int>& col, juce::Label& l, juce::Slider& s)
        {
            auto r = row (col);
            l.setBounds (r.removeFromLeft (80));
            s.setBounds (r);
        };
        valueRow (left,  inLLabel,  inLSlider);
        valueRow (left,  inRLabel,  inRSlider);
        valueRow (right, outLLabel, outLSlider);
        valueRow (right, outRLabel, outRSlider);

        area.removeFromTop (6);
        statusLabel.setBounds (area.removeFromTop (20));
    }

private:
    struct Trim
    {
        juce::Label label, field, info;
        juce::TextButton down { juce::String::fromUTF8 ("\xe2\x97\x80") },   // left arrow
                         up   { juce::String::fromUTF8 ("\xe2\x96\xb6") };   // right arrow
        int   steps = 0;      // trim in 0.01 dB
        float base  = 18.0f;  // output calibration at trim 0
    };

    // Trim of the outputs, saved with the plugin state
    static juce::Identifier trimProperty (int ch)  { return ch == 0 ? "calTrimL" : "calTrimR"; }

    static double parseDb (const juce::String& text)
    {
        return text.retainCharacters ("-+0123456789.,").replaceCharacter (',', '.').getDoubleValue();
    }

    static juce::String fmt (float db)  { return (db > 0.0f ? "+" : "") + juce::String (db, 2); }

    static void styleDisplay (juce::Label& l)
    {
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold)));
        l.setColour (juce::Label::backgroundColourId, DevTestColours::text);
        l.setColour (juce::Label::textColourId,       DevTestColours::accent);
        l.setColour (juce::Label::textWhenEditingColourId, DevTestColours::text);
    }

    static void styleInfo (juce::Label& l)
    {
        l.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
        l.setColour (juce::Label::textColourId, DevTestColours::text);
    }

    void changed()
    {
        if (onCalibrationChanged != nullptr)
            onCalibrationChanged();
    }

    // Level of the test tone in dBFS for one output (+4 dBu with its calibration)
    float toneLevelDb (int ch) const  { return toneDbu - processor.getOutputCalibration (ch); }

    void updateTone()
    {
        processor.setCalibrationTone (toneButton.getToggleState(), toneLevelDb (0), toneLevelDb (1));
        toneButton.setButtonText (toneButton.getToggleState() ? "Test tone on" : "Test tone off");
    }

    void stepTrim (int ch, int direction)
    {
        const int amount = juce::ModifierKeys::getCurrentModifiers().isShiftDown() ? 10 : 1;
        setTrim (ch, trims[(size_t) ch].steps + direction * amount);
    }

    // More trim = higher output voltage = less dBu per dBFS
    void setTrim (int ch, int steps)
    {
        auto& t = trims[(size_t) ch];
        t.steps = juce::jlimit (-maxTrim, maxTrim, steps);
        processor.apvts.state.setProperty (trimProperty (ch), t.steps, nullptr);
        processor.setOutputCalibration (ch, t.base - (float) t.steps * stepDb);

        syncValues();
        updateTrimDisplays();
        updateTone();
        changed();
    }

    void outputValueTyped (int ch, float dbuAtFullScale)
    {
        if (std::abs (dbuAtFullScale - processor.getOutputCalibration (ch)) < 0.0005f)
            return;   // set by the trim itself

        auto& t = trims[(size_t) ch];
        t.base  = dbuAtFullScale;
        t.steps = 0;
        processor.apvts.state.setProperty (trimProperty (ch), 0, nullptr);
        processor.setOutputCalibration (ch, dbuAtFullScale);
        updateTrimDisplays();
        updateTone();
        changed();
    }

    void syncValues()
    {
        inLSlider.setValue  (processor.getInputCalibration (0),  juce::dontSendNotification);
        inRSlider.setValue  (processor.getInputCalibration (1),  juce::dontSendNotification);
        outLSlider.setValue (processor.getOutputCalibration (0), juce::dontSendNotification);
        outRSlider.setValue (processor.getOutputCalibration (1), juce::dontSendNotification);
        for (auto* s : { &inLSlider, &inRSlider, &outLSlider, &outRSlider })
            s->updateText();
    }

    void updateTrimDisplays()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& t = trims[(size_t) ch];
            t.field.setText ((t.steps > 0 ? "+" : "") + juce::String (t.steps), juce::dontSendNotification);

            const float level = toneLevelDb (ch);
            t.info.setText (fmt ((float) t.steps * stepDb) + " dB   tone " + fmt (level) + " dBFS"
                                + (level > 0.0f ? "  (clipped!)" : ""),
                            juce::dontSendNotification);
        }
    }

    void calibrateInputs()
    {
        if (! toneButton.getToggleState())
        {
            setStatus ("Switch the test tone on and connect output L -> input L, output R -> input R.");
            return;
        }

        juce::StringArray missing;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float level = processor.getInputLevelDb (ch);
            if (level < -50.0f)
            {
                missing.add (ch == 0 ? "L" : "R");
                continue;
            }
            processor.setInputCalibration (ch, toneDbu - level);
        }

        syncValues();
        changed();
        setStatus (missing.isEmpty() ? "Inputs calibrated: they now show +4.00 dBu."
                                     : "No tone on input " + missing.joinIntoString (" and ") + " - not changed.");
    }

    void setStatus (const juce::String& message)
    {
        statusLabel.setText (message, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float level = processor.getInputLevelDb (ch);
            const auto  text  = juce::String (ch == 0 ? "In L  " : "In R  ")
                              + (level < -100.0f ? juce::String ("no signal")
                                                 : fmt (level) + " dBFS = " + fmt (level + processor.getInputCalibration (ch)) + " dBu");
            (ch == 0 ? inLMeter : inRMeter).setText (text, juce::dontSendNotification);
        }

        toneInfo.setText (toneButton.getToggleState() ? "target: +4 dBu = 1.228 V RMS" : juce::String(),
                          juce::dontSendNotification);
    }

    AudioPluginAudioProcessor& processor;
    std::function<void()> onCalibrationChanged;

    juce::GroupComponent outGroup, inGroup, valueGroup;
    juce::Label outHint, inHint, statusLabel;

    juce::TextButton toneButton { "Test tone off" };
    juce::Label toneInfo;
    std::array<Trim, 2> trims;

    juce::TextButton calibrateInButton { "CALIBRATE INPUTS" };
    juce::Label inLMeter, inRMeter;

    juce::Label inLLabel, inRLabel, outLLabel, outRLabel;
    juce::Slider inLSlider, inRSlider, outLSlider, outRSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalibrationComponent)
};
