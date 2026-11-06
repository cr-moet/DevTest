#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "DevTestLookAndFeel.h"

//==============================================================================
// FFT spectrum of both inputs (L and R, the same signals as the scope).
// Levels are calibrated to dBFS: a sine with a -12 dBFS peak shows up as a
// line at -12 dB (most accurate with the Flat-Top window).
// The component is transparent so the editor's background texture shows through.
//==============================================================================
class SpectrumComponent final : public juce::Component,
                                private juce::Timer
{
public:
    explicit SpectrumComponent (AudioPluginAudioProcessor& p)
        : processor (p)
    {
        sizeParam   = processor.apvts.getRawParameterValue (ParamIDs::fftSize);
        windowParam = processor.apvts.getRawParameterValue (ParamIDs::fftWindow);
        avgParam    = processor.apvts.getRawParameterValue (ParamIDs::fftAverage);
        rangeParam  = processor.apvts.getRawParameterValue (ParamIDs::fftRange);
        maxFreqParam = processor.apvts.getRawParameterValue (ParamIDs::fftMaxFreq);

        setOpaque (false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace DevTestColours;

        const auto plot = getPlotArea();
        drawGrid (g, plot);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());

            drawSpectrum (g, plot, dbR, traceR, processor.inputOffsetDb (1));   // R first, so L lies on top
            drawSpectrum (g, plot, dbL, traceL, processor.inputOffsetDb (0));
        }

        g.setColour (text.withAlpha (0.6f));
        g.drawRect (plot, 1.0f);

        drawInfo (g, plot);
    }

private:
    static constexpr float minFreq = 20.0f;

    juce::Rectangle<float> getPlotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (44.0f).withTrimmedRight (16.0f)
                                         .withTrimmedTop (24.0f).withTrimmedBottom (40.0f);
    }

    // Upper end of the frequency axis: selected range, limited by the Nyquist frequency
    float getSelectedMaxFreq() const
    {
        static constexpr float choices[] = { 20000.0f, 50000.0f, 100000.0f };
        return choices[juce::jlimit (0, 2, juce::roundToInt (maxFreqParam->load()))];
    }

    float getMaxFreq() const    { return juce::jmin (getSelectedMaxFreq(), (float) (sampleRate * 0.5)); }
    float getRangeDb() const    { return 60.0f + 30.0f * (float) juce::roundToInt (rangeParam->load()); }

    float freqToX (float f, juce::Rectangle<float> plot) const
    {
        return plot.getX() + plot.getWidth() * std::log (f / minFreq) / std::log (getMaxFreq() / minFreq);
    }

    float xToFreq (float x, juce::Rectangle<float> plot) const
    {
        return minFreq * std::pow (getMaxFreq() / minFreq, (x - plot.getX()) / plot.getWidth());
    }

    // Level step of the grid lines
    float getGridStepDb() const  { return getRangeDb() > 90.0f ? 20.0f : 10.0f; }

    // Top of the level axis: 0 dBFS, or in dBu the next grid line above the
    // calibrated full-scale level
    float getTopDb() const
    {
        if (! processor.isUnitDbu())
            return 0.0f;

        const float step = getGridStepDb();
        const float fullScale = juce::jmax (processor.inputOffsetDb (0), processor.inputOffsetDb (1));
        return std::ceil (fullScale / step - 0.001f) * step;
    }

    // Level in display units (dBFS or dBu) -> y
    float dbToY (float db, juce::Rectangle<float> plot) const
    {
        const float range = getRangeDb();
        return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (getTopDb() - db) / range);
    }

    //==========================================================================
    void prepareFft (int order, int windowType)
    {
        fftOrder    = order;
        windowIndex = windowType;
        fftSize     = 1 << order;

        fft = std::make_unique<juce::dsp::FFT> (order);
        fftData.assign ((size_t) fftSize * 2, 0.0f);
        window.assign ((size_t) fftSize, 0.0f);

        using WF = juce::dsp::WindowingFunction<float>;
        static constexpr WF::WindowingMethod methods[] = { WF::hann, WF::blackmanHarris, WF::flatTop, WF::rectangular };
        WF::fillWindowingTables (window.data(), (size_t) fftSize, methods[juce::jlimit (0, 3, windowType)], false);

        // Coherent gain of the window -> level correction to dBFS
        double sum = 0.0;
        for (auto w : window) sum += w;
        amplitudeScale = (float) (2.0 / sum);

        const auto numBins = (size_t) fftSize / 2 + 1;
        powerL.assign (numBins, 0.0f);
        powerR.assign (numBins, 0.0f);
        dbL.assign (numBins, -200.0f);
        dbR.assign (numBins, -200.0f);
        firstFrame = true;
    }

    void analyse (bool left, uint64_t start, float alpha, std::vector<float>& power, std::vector<float>& db)
    {
        const auto& buf = processor.scopeBuffer;

        for (int i = 0; i < fftSize; ++i)
        {
            const auto idx = start + (uint64_t) i;
            fftData[(size_t) i] = (left ? buf.getL (idx) : buf.getR (idx)) * window[(size_t) i];
        }
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

        fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

        for (size_t k = 0; k < power.size(); ++k)
        {
            const float mag = fftData[k] * amplitudeScale;
            const float pw  = mag * mag;
            power[k] = firstFrame ? pw : power[k] + alpha * (pw - power[k]);
            db[k]    = 10.0f * std::log10 (power[k] + 1.0e-20f);
        }
    }

    // Find the highest peak and refine it between bins with a parabolic fit
    void findPeak (const std::vector<float>& db, float& freq, float& level) const
    {
        const auto numBins = (int) db.size();
        const int  maxBin  = juce::jmin (numBins - 2, (int) (getMaxFreq() / (float) sampleRate * (float) fftSize));
        int best = 2;

        for (int k = 3; k <= maxBin; ++k)
            if (db[(size_t) k] > db[(size_t) best])
                best = k;

        const float a = db[(size_t) best - 1], b = db[(size_t) best], c = db[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        const float delta = std::abs (denom) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom) : 0.0f;

        freq  = ((float) best + delta) * (float) sampleRate / (float) fftSize;
        level = b - 0.25f * (a - c) * delta;
    }

    void timerCallback() override
    {
        if (! isShowing())
            return; // other tab selected -> skip all work

        sampleRate = processor.getCurrentSampleRate();

        const int order      = 10 + juce::roundToInt (sizeParam->load());
        const int windowType = juce::roundToInt (windowParam->load());

        if (fft == nullptr || order != fftOrder || windowType != windowIndex || ! juce::approximatelyEqual (sampleRate, lastSampleRate))
        {
            prepareFft (order, windowType);
            lastSampleRate = sampleRate;
        }

        const auto writePos = processor.scopeBuffer.getWritePos();
        if (writePos < (uint64_t) fftSize + 1)
            return;

        static constexpr float alphas[] = { 1.0f, 0.5f, 0.2f, 0.05f };
        const float alpha = alphas[juce::jlimit (0, 3, juce::roundToInt (avgParam->load()))];

        const auto start = writePos - (uint64_t) fftSize;
        analyse (true,  start, alpha, powerL, dbL);
        analyse (false, start, alpha, powerR, dbR);
        firstFrame = false;

        findPeak (dbL, peakFreqL, peakLevelL);
        findPeak (dbR, peakFreqR, peakLevelR);

        repaint();
    }

    //==========================================================================
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace DevTestColours;

        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        // Frequency lines
        static constexpr int freqs[] = { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                           1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000,
                                           30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };
        for (auto f : freqs)
        {
            if ((float) f > getMaxFreq())
                break;

            const float x = freqToX ((float) f, plot);
            const bool  major = (f == 20 || f == 50 || f == 100 || f == 200 || f == 500 || f == 1000
                                 || f == 2000 || f == 5000 || f == 10000 || f == 20000 || f == 50000 || f == 100000);
            g.setColour (major ? gridStrong.withAlpha (0.25f) : grid.withAlpha (0.08f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);

            if (major)
            {
                g.setColour (text.withAlpha (0.7f));
                const auto label = f >= 1000 ? juce::String (f / 1000) + "k" : juce::String (f);
                g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f),
                            juce::Justification::centred);
            }
        }

        // Level lines
        const float range = getRangeDb();
        const float step  = getGridStepDb();
        const float top   = getTopDb();

        for (int i = 0; (float) i * step <= range; ++i)
        {
            const float db = top - (float) i * step;
            const float y  = dbToY (db, plot);
            g.setColour (i == 0 ? gridStrong : grid);
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f);

            g.setColour (text.withAlpha (0.7f));
            g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 4.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }

    // offset: dBFS -> display units (0 in dBFS mode, calibration value in dBu mode)
    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> plot,
                       const std::vector<float>& db, juce::Colour colour, float offset) const
    {
        if (db.size() < 4)
            return;

        const float binWidth = (float) sampleRate / (float) fftSize;
        const int   lastBin  = (int) db.size() - 1;

        juce::Path path;
        const int width = (int) plot.getWidth();

        for (int px = 0; px <= width; ++px)
        {
            const float x  = plot.getX() + (float) px;
            const float f0 = xToFreq (x - 0.5f, plot) / binWidth;
            const float f1 = xToFreq (x + 0.5f, plot) / binWidth;

            float value;
            const int b0 = (int) std::ceil (f0), b1 = (int) std::floor (f1);

            if (b1 >= b0)
            {
                // Several bins per pixel: take the maximum (no peak gets lost)
                value = -300.0f;
                for (int b = juce::jmax (1, b0); b <= juce::jmin (lastBin, b1); ++b)
                    value = juce::jmax (value, db[(size_t) b]);
            }
            else
            {
                // Less than one bin per pixel: interpolate between bins
                const float pos  = juce::jlimit (1.0f, (float) lastBin - 1.0f, (f0 + f1) * 0.5f);
                const int   i    = (int) pos;
                const float frac = pos - (float) i;
                value = db[(size_t) i] + frac * (db[(size_t) i + 1] - db[(size_t) i]);
            }

            const float y = dbToY (value + offset, plot);
            if (px == 0) path.startNewSubPath (x, y);
            else         path.lineTo (x, y);
        }

        // Light fill below the curve
        juce::Path fill (path);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.07f));
        g.fillPath (fill);

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.3f));
    }

    void drawInfo (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace DevTestColours;

        const auto unit = processor.getLevelUnit();
        const auto fmtPeak = [&] (float f, float level, int ch)
        {
            if (level < -150.0f)
                return juce::String ("--");
            return (f < 1000.0f ? juce::String (f, 1) + " Hz" : juce::String (f / 1000.0f, 3) + " kHz")
                       + "   " + juce::String (level + processor.inputOffsetDb (ch), 1) + " " + unit;
        };

        const float y = plot.getY() - 20.0f;

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.setColour (traceL);
        g.drawText ("L  Peak " + fmtPeak (peakFreqL, peakLevelL, 0),
                    juce::Rectangle<float> (plot.getX(), y, plot.getWidth() * 0.5f, 16.0f),
                    juce::Justification::centredLeft);

        g.setColour (traceR);
        g.drawText ("R  Peak " + fmtPeak (peakFreqR, peakLevelR, 1),
                    juce::Rectangle<float> (plot.getCentreX(), y, plot.getWidth() * 0.5f, 16.0f),
                    juce::Justification::centredRight);

        // Note when the sample rate does not allow the selected range
        const juce::String limitNote = getMaxFreq() < getSelectedMaxFreq() - 1.0f
                                           ? "   |   range limited to " + juce::String (getMaxFreq() / 1000.0f, 1) + " kHz by sample rate"
                                           : juce::String();

        g.setColour (text.withAlpha (0.7f));
        g.drawText ("Resolution " + juce::String (sampleRate / fftSize, 2) + " Hz/bin   |   "
                        + juce::String (sampleRate / 1000.0, 1) + " kHz" + limitNote,
                    juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f),
                    juce::Justification::centredRight);
    }

    //==========================================================================
    AudioPluginAudioProcessor& processor;

    std::atomic<float>* sizeParam   = nullptr;
    std::atomic<float>* windowParam = nullptr;
    std::atomic<float>* avgParam    = nullptr;
    std::atomic<float>* rangeParam  = nullptr;
    std::atomic<float>* maxFreqParam = nullptr;

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftOrder = 0, fftSize = 0, windowIndex = -1;
    std::vector<float> fftData, window, powerL, powerR, dbL, dbR;
    float amplitudeScale = 1.0f;
    bool  firstFrame = true;

    double sampleRate = 48000.0, lastSampleRate = 0.0;
    float peakFreqL = 0.0f, peakLevelL = -200.0f, peakFreqR = 0.0f, peakLevelR = -200.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumComponent)
};
