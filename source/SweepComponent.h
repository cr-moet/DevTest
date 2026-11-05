#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "DevTestLookAndFeel.h"

//==============================================================================
// Frequency response display (amplitude over frequency) for inputs L and R.
//
// Two modes:
//  - Sine sweep: the processor measures the returned level relative to the
//    sent level during the sweep; the curve is drawn live. In continuous mode
//    the sweeps repeat and each new sweep overwrites the curve from left to right.
//  - Impulse (Dirac): the processor sends one single-sample impulse per interval.
//    The returned signal of each interval is an impulse response; several are
//    averaged synchronously (better signal-to-noise ratio). The averaged impulse
//    response is windowed ("IR window") and transformed into a frequency response
//    with an FFT, optionally smoothed in fractions of an octave. In continuous
//    mode the display is updated after every impulse (moving average).
//    The FFT work runs on a background thread so the GUI stays responsive.
//==============================================================================
class SweepComponent final : public juce::Component,
                             private juce::Timer
{
public:
    explicit SweepComponent (AudioPluginAudioProcessor& p)
        : processor (p)
    {
        modeParam   = processor.apvts.getRawParameterValue (ParamIDs::swMode);
        avgParam    = processor.apvts.getRawParameterValue (ParamIDs::swAverages);
        gateParam   = processor.apvts.getRawParameterValue (ParamIDs::swGate);
        smoothParam = processor.apvts.getRawParameterValue (ParamIDs::swSmooth);
        rangeParam  = processor.apvts.getRawParameterValue (ParamIDs::swRange);
        normParam   = processor.apvts.getRawParameterValue (ParamIDs::swNormalize);

        readBuffer.resize (4096);
        setOpaque (false);
        startTimerHz (30);
    }

    ~SweepComponent() override
    {
        pool.removeAllJobs (true, 20000);
    }

    // Called before a new test starts
    void clear()
    {
        points.clear();
        previousPoints.clear();
        sweepsDone = 0;
        impulse.reset();
        spectrum.reset();
        averageL.clear();
        averageR.clear();
        impulsesReceived = 0;
        lastPeriodRead = 0;
        analysing = false;
        spectrumDirty = false;
        ++jobCounter;   // results of older jobs are ignored
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace DevTestColours;

        const auto areas = getAreas();

        drawGrid (g, areas.plot);

        // Collect the two curves (live points or impulse response spectrum)
        Curve curveL, curveR;
        getCurves (curveL, curveR);

        const float refL = normalise() ? curveL.valueAt (1000.0f, 0.0f) : 0.0f;
        const float refR = normalise() ? curveR.valueAt (1000.0f, 0.0f) : 0.0f;

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (areas.plot.toNearestInt());
            drawCurve (g, areas.plot, curveR, refR, traceR);
            drawCurve (g, areas.plot, curveL, refL, traceL);
        }

        g.setColour (text.withAlpha (0.6f));
        g.drawRect (areas.plot, 1.0f);

        if (! areas.ir.isEmpty())
            drawImpulse (g, areas.ir);

        drawInfo (g, areas.info, curveL, curveR);
    }

private:
    //==========================================================================
    // A curve: frequencies (ascending) with levels in dB
    struct Curve
    {
        std::vector<float> freq, db;

        bool empty() const noexcept { return freq.size() < 2; }

        // Level at a frequency (linear interpolation on a log frequency axis)
        float valueAt (float f, float fallback) const
        {
            if (empty() || f < freq.front() || f > freq.back())
                return fallback;

            const auto it = std::lower_bound (freq.begin(), freq.end(), f);
            const auto i  = (size_t) juce::jmax (1, (int) std::distance (freq.begin(), it));
            const float f0 = freq[i - 1], f1 = freq[i];
            const float t  = f1 > f0 ? (std::log (f / f0) / std::log (f1 / f0)) : 0.0f;
            return db[i - 1] + juce::jlimit (0.0f, 1.0f, t) * (db[i] - db[i - 1]);
        }

        // -3 dB points around 1 kHz (relative to the level at 1 kHz).
        // lowFound / highFound are false if the curve does not drop by 3 dB
        // within the measured range (the band edge lies outside of it).
        bool findBandwidth (float& low, float& high, bool& lowFound, bool& highFound) const
        {
            if (empty() || freq.front() > 1000.0f || freq.back() < 1000.0f)
                return false;

            const float ref = valueAt (1000.0f, 0.0f) - 3.0f;
            const auto start = (size_t) std::distance (freq.begin(), std::lower_bound (freq.begin(), freq.end(), 1000.0f));

            low = freq.front();
            lowFound = false;
            for (size_t i = juce::jmin (start, freq.size() - 1); i > 0; --i)
                if (db[i - 1] < ref)
                {
                    const float t = (ref - db[i - 1]) / juce::jmax (1.0e-6f, db[i] - db[i - 1]);
                    low = freq[i - 1] * std::pow (freq[i] / freq[i - 1], juce::jlimit (0.0f, 1.0f, t));
                    lowFound = true;
                    break;
                }

            high = freq.back();
            highFound = false;
            for (size_t i = start; i + 1 < freq.size(); ++i)
                if (db[i + 1] < ref)
                {
                    const float t = (db[i] - ref) / juce::jmax (1.0e-6f, db[i] - db[i + 1]);
                    high = freq[i] * std::pow (freq[i + 1] / freq[i], juce::jlimit (0.0f, 1.0f, t));
                    highFound = true;
                    break;
                }

            return true;
        }
    };

    // Averaged impulse responses (normalised to the impulse amplitude)
    struct ImpulseData
    {
        std::vector<float> irL, irR;   // one interval each (circular)
        double sampleRate = 48000.0;
        float  endFreq = 20000.0f;
        int    peak = 0;               // index of the main peak (= latency in samples)
    };

    // Result of windowing + FFT + smoothing
    struct SpectrumData
    {
        Curve left, right;
        std::vector<float> viewL, viewR;   // windowed impulse response for the small IR display
        float viewStartMs = 0.0f, viewEndMs = 0.0f;
    };

    bool normalise() const      { return normParam->load() > 0.5f; }
    bool impulseMode() const    { return juce::roundToInt (modeParam->load()) == (int) SweepMode::impulse; }
    float getRangeDb() const
    {
        static constexpr float ranges[] = { 3.0f, 12.0f, 24.0f, 48.0f };
        return ranges[juce::jlimit (0, 3, juce::roundToInt (rangeParam->load()))];
    }

    float getMaxFreq() const
    {
        if (impulse != nullptr)  return impulse->endFreq;
        if (! points.empty())    return juce::jmax (processor.getSweepEndFrequency(), points.back().freq);
        return processor.getSweepEndFrequency();
    }

    static constexpr float minFreq = 10.0f;

    //==========================================================================
    struct Areas { juce::Rectangle<float> info, plot, ir; };

    Areas getAreas() const
    {
        Areas a;
        auto r = getLocalBounds().toFloat();
        a.info = r.removeFromTop (58.0f).reduced (12.0f, 2.0f);

        r = r.withTrimmedLeft (44.0f).withTrimmedRight (16.0f).withTrimmedBottom (4.0f);

        if (impulseMode() && spectrum != nullptr)
        {
            a.ir = r.removeFromBottom (juce::jmax (60.0f, r.getHeight() * 0.22f)).withTrimmedTop (22.0f);
            r.removeFromBottom (4.0f);
        }

        a.plot = r.withTrimmedBottom (18.0f);
        return a;
    }

    float freqToX (float f, juce::Rectangle<float> plot) const
    {
        return plot.getX() + plot.getWidth() * std::log (juce::jmax (minFreq, f) / minFreq) / std::log (getMaxFreq() / minFreq);
    }

    float dbToY (float db, juce::Rectangle<float> plot) const
    {
        const float range = getRangeDb();
        return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.5f, 1.5f, db / range);
    }

    //==========================================================================
    void getCurves (Curve& left, Curve& right) const
    {
        if (impulseMode())
        {
            if (spectrum != nullptr)
            {
                left  = spectrum->left;
                right = spectrum->right;
            }
            return;
        }

        const auto add = [&left, &right] (const SweepPoint& p)
        {
            if (! left.freq.empty() && p.freq <= left.freq.back())
                return;   // keep the frequencies strictly ascending

            left.freq.push_back (p.freq);   left.db.push_back (p.gainL);
            right.freq.push_back (p.freq);  right.db.push_back (p.gainR);
        };

        // Current sweep, then the rest of the previous sweep (continuous mode)
        for (const auto& p : points)          add (p);
        for (const auto& p : previousPoints)  add (p);
    }

    //==========================================================================
    void timerCallback() override
    {
        // Data is collected even when the tab is hidden, so no impulse gets lost
        const auto runId = processor.getSweepRunId();

        for (;;)
        {
            const int n = processor.readSweepPoints (readBuffer.data(), (int) readBuffer.size());
            if (n <= 0)
                break;

            for (int i = 0; i < n; ++i)
            {
                const auto& p = readBuffer[(size_t) i];
                if (p.runId != runId)
                    continue;

                // A clearly lower frequency means a new sweep has started (continuous mode):
                // the last complete sweep is kept and gradually replaced by the new one
                if (! points.empty() && p.freq < points.back().freq * 0.5f)
                {
                    previousPoints = std::move (points);
                    points.clear();
                    ++sweepsDone;
                }

                points.push_back (p);
            }
        }

        // Impulse mode: collect the impulse responses of the finished intervals
        if (processor.getImpulseActiveRunId() == runId)
            collectImpulses();

        if (! isShowing())
            return;   // no analysis / drawing while another tab is shown

        // Window or smoothing changed -> recompute the spectrum
        const int gate = juce::roundToInt (gateParam->load()), smooth = juce::roundToInt (smoothParam->load());
        if (impulsesReceived > 0 && (gate != lastGate || smooth != lastSmooth))
            spectrumDirty = true;

        // Only one spectrum job at a time; new data waits for the next timer tick
        if (spectrumDirty && ! analysing)
            startSpectrumAnalysis();

        repaint();
    }

    // Synchronous averaging: the impulse responses of all intervals are averaged
    // sample by sample. In continuous mode this becomes a moving average over the
    // selected number of impulses.
    void collectImpulses()
    {
        static constexpr int averageChoices[] = { 1, 4, 16, 64 };
        const int maxAverages = averageChoices[juce::jlimit (0, 3, juce::roundToInt (avgParam->load()))];

        const auto done = processor.getImpulsePeriodsDone();

        while (lastPeriodRead < done)
        {
            ++lastPeriodRead;

            if (! processor.readImpulsePeriod (lastPeriodRead, tempL, tempR))
                continue;   // too old (GUI was busy), skip it

            if (averageL.size() != tempL.size())
            {
                averageL.assign (tempL.size(), 0.0f);
                averageR.assign (tempR.size(), 0.0f);
                impulsesReceived = 0;
            }

            ++impulsesReceived;
            const float weight = 1.0f / (float) juce::jmin (impulsesReceived, maxAverages);

            for (size_t i = 0; i < tempL.size(); ++i)
            {
                averageL[i] += weight * (tempL[i] - averageL[i]);
                averageR[i] += weight * (tempR[i] - averageR[i]);
            }

            spectrumDirty = true;
        }
    }

    //==========================================================================
    // Stage 2: window the impulse response, FFT, smoothing (background thread)
    void startSpectrumAnalysis()
    {
        if (averageL.empty())
            return;

        spectrumDirty = false;
        lastGate   = juce::roundToInt (gateParam->load());
        lastSmooth = juce::roundToInt (smoothParam->load());

        // Snapshot of the current average, normalised to the impulse amplitude
        auto data = std::make_shared<ImpulseData>();
        const float scale = 1.0f / juce::jmax (1.0e-9f, processor.getImpulseAmplitude());
        data->irL.resize (averageL.size());
        data->irR.resize (averageR.size());
        for (size_t i = 0; i < averageL.size(); ++i)
        {
            data->irL[i] = averageL[i] * scale;
            data->irR[i] = averageR[i] * scale;
        }
        data->sampleRate = processor.getImpulseSampleRate();
        data->endFreq    = processor.getSweepEndFrequency();

        // Main peak (latency of the loop): strongest sample of both channels
        float best = -1.0f;
        for (size_t i = 0; i < data->irL.size(); ++i)
        {
            const float v = std::abs (data->irL[i]) + std::abs (data->irR[i]);
            if (v > best) { best = v; data->peak = (int) i; }
        }

        const int jobId = ++jobCounter;
        analysing = true;
        juce::Component::SafePointer<SweepComponent> safe (this);
        const int gate = lastGate, smooth = lastSmooth;

        pool.addJob ([safe, jobId, data, gate, smooth]
        {
            auto result = std::make_shared<SpectrumData> (computeSpectrum (*data, gate, smooth));

            juce::MessageManager::callAsync ([safe, jobId, data, result]
            {
                if (safe == nullptr || jobId != safe->jobCounter)
                    return;

                safe->impulse   = data;
                safe->spectrum  = result;
                safe->analysing = false;
                safe->repaint();
            });
        });
    }

    static SpectrumData computeSpectrum (const ImpulseData& d, int gateIndex, int smoothIndex)
    {
        SpectrumData out;
        const int n = (int) d.irL.size();
        if (n < 4)
            return out;

        // Window: from 1 ms before the peak to the selected length after it
        static constexpr double gates[] = { 0.005, 0.02, 0.1, 0.5, -1.0 };
        const double gateSeconds = gates[juce::jlimit (0, 4, gateIndex)];

        const int pre    = juce::jmax (16, (int) (0.001 * d.sampleRate));
        const int length = gateSeconds < 0.0 ? n - pre
                                             : juce::jmin (n - pre, (int) (gateSeconds * d.sampleRate));
        const int total  = pre + length;

        int order = 16;   // at least 65536 points (zero padding) for a smooth curve
        while ((1 << order) < total)
            ++order;
        const int m = 1 << order;

        juce::dsp::FFT fft (order);
        const auto mBins = m / 2 + 1;

        const auto windowed = [&] (const std::vector<float>& ir, std::vector<float>& buffer, std::vector<float>* view)
        {
            buffer.assign ((size_t) m * 2, 0.0f);

            for (int i = 0; i < total; ++i)
            {
                const int src = ((d.peak - pre + i) % n + n) % n;   // circular (pre-ringing wraps around)

                // Half-Hann fade-in over the pre part, fade-out over the last 20 % (not for "Full")
                float w = 1.0f;
                if (i < pre)
                    w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) pre);
                else if (gateSeconds > 0.0)
                {
                    const int fadeLen = juce::jmax (1, length / 5);
                    const int fromEnd = total - 1 - i;
                    if (fromEnd < fadeLen)
                        w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) fromEnd / (float) fadeLen);
                }

                buffer[(size_t) i] = ir[(size_t) src] * w;
            }

            // Reduced copy of the windowed IR for the small display (max. 2000 points, peak-preserving)
            if (view != nullptr)
            {
                const int points = juce::jmin (2000, total);
                view->assign ((size_t) points, 0.0f);
                for (int p = 0; p < points; ++p)
                {
                    const int a = (int) ((int64_t) p * total / points), b = juce::jmax (a + 1, (int) ((int64_t) (p + 1) * total / points));
                    float v = 0.0f;
                    for (int i = a; i < b; ++i)
                        if (std::abs (buffer[(size_t) i]) > std::abs (v))
                            v = buffer[(size_t) i];
                    (*view)[(size_t) p] = v;
                }
            }

            fft.performRealOnlyForwardTransform (buffer.data(), true);

            // Power per bin
            for (int k = 0; k < mBins; ++k)
                buffer[(size_t) k] = buffer[(size_t) (2 * k)] * buffer[(size_t) (2 * k)]
                                   + buffer[(size_t) (2 * k + 1)] * buffer[(size_t) (2 * k + 1)];
        };

        std::vector<float> powL, powR;
        windowed (d.irL, powL, &out.viewL);
        windowed (d.irR, powR, &out.viewR);

        out.viewStartMs = (float) (-1000.0 * pre / d.sampleRate);
        out.viewEndMs   = (float) (1000.0 * length / d.sampleRate);

        // Display curve: 1000 log-spaced points, fractional-octave smoothing (power average)
        static constexpr float fractions[] = { 0.0f, 24.0f, 12.0f, 6.0f, 3.0f };
        const float fraction = fractions[juce::jlimit (0, 4, smoothIndex)];
        const float binHz    = (float) (d.sampleRate / m);
        constexpr int numPoints = 1000;

        for (int p = 0; p < numPoints; ++p)
        {
            const float f = minFreq * std::pow (d.endFreq / minFreq, (float) p / (float) (numPoints - 1));

            float lo = f / binHz, hi = lo;
            if (fraction > 0.0f)
            {
                const float half = std::pow (2.0f, 0.5f / fraction);
                lo = f / half / binHz;
                hi = f * half / binHz;
            }

            const int b0 = juce::jmax (1, (int) std::ceil (lo)), b1 = juce::jmin (mBins - 1, (int) std::floor (hi));
            float pl, pr;

            if (b1 >= b0)
            {
                double sl = 0.0, sr = 0.0;
                for (int b = b0; b <= b1; ++b) { sl += powL[(size_t) b]; sr += powR[(size_t) b]; }
                pl = (float) (sl / (b1 - b0 + 1));
                pr = (float) (sr / (b1 - b0 + 1));
            }
            else
            {
                // Narrower than one bin: interpolate between the neighbouring bins
                const float pos = juce::jlimit (1.0f, (float) mBins - 2.0f, (lo + hi) * 0.5f);
                const int   i   = (int) pos;
                const float t   = pos - (float) i;
                pl = powL[(size_t) i] + t * (powL[(size_t) i + 1] - powL[(size_t) i]);
                pr = powR[(size_t) i] + t * (powR[(size_t) i + 1] - powR[(size_t) i]);
            }

            out.left.freq.push_back (f);
            out.right.freq.push_back (f);
            out.left.db.push_back  (10.0f * std::log10 (pl + 1.0e-20f));
            out.right.db.push_back (10.0f * std::log10 (pr + 1.0e-20f));
        }

        return out;
    }

    //==========================================================================
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace DevTestColours;

        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        static constexpr int freqs[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                         1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000,
                                         30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };
        for (auto f : freqs)
        {
            if ((float) f > getMaxFreq() * 1.001f)
                break;

            const float x = freqToX ((float) f, plot);
            const bool major = (f == 10 || f == 20 || f == 50 || f == 100 || f == 200 || f == 500 || f == 1000
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

        const float range = getRangeDb();
        const float step  = range <= 3.0f ? 1.0f : (range <= 12.0f ? 3.0f : (range <= 24.0f ? 6.0f : 12.0f));

        for (float db = -range; db <= range + 0.01f; db += step)
        {
            const float y = dbToY (db, plot);
            g.setColour (std::abs (db) < 0.01f ? gridStrong : grid);
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f);

            g.setColour (text.withAlpha (0.7f));
            g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db),
                        juce::Rectangle<float> (plot.getX() - 40.0f, y - 7.0f, 36.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }

    void drawCurve (juce::Graphics& g, juce::Rectangle<float> plot, const Curve& c, float ref, juce::Colour colour) const
    {
        if (c.empty())
            return;

        juce::Path path;
        bool started = false;

        for (size_t i = 0; i < c.freq.size(); ++i)
        {
            if (c.freq[i] < minFreq)
                continue;

            const juce::Point<float> pt (freqToX (c.freq[i], plot), dbToY (c.db[i] - ref, plot));
            if (! started) { path.startNewSubPath (pt); started = true; }
            else           path.lineTo (pt);
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
    }

    void drawImpulse (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        using namespace DevTestColours;

        g.setColour (text.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("Impulse response (windowed)   " + juce::String (spectrum->viewStartMs, 1) + " ... "
                        + juce::String (spectrum->viewEndMs, 1) + " ms",
                    area.withY (area.getY() - 18.0f).withHeight (16.0f), juce::Justification::centredLeft);

        g.setColour (grid);
        g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);

        // Time 0 (main peak)
        const float span = spectrum->viewEndMs - spectrum->viewStartMs;
        if (span > 0.0f)
        {
            const float x0 = area.getX() + area.getWidth() * (-spectrum->viewStartMs / span);
            g.setColour (gridStrong);
            g.drawLine (x0, area.getY(), x0, area.getBottom(), 1.0f);
        }

        // Common scale for both channels
        float peak = 1.0e-9f;
        for (auto v : spectrum->viewL) peak = juce::jmax (peak, std::abs (v));
        for (auto v : spectrum->viewR) peak = juce::jmax (peak, std::abs (v));

        const auto draw = [&] (const std::vector<float>& v, juce::Colour colour)
        {
            if (v.size() < 2)
                return;

            juce::Path p;
            for (size_t i = 0; i < v.size(); ++i)
            {
                const float x = area.getX() + area.getWidth() * (float) i / (float) (v.size() - 1);
                const float y = area.getCentreY() - area.getHeight() * 0.48f * v[i] / peak;
                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        };

        draw (spectrum->viewR, traceR);
        draw (spectrum->viewL, traceL);

        g.setColour (text.withAlpha (0.6f));
        g.drawRect (area, 1.0f);
    }

    void drawInfo (juce::Graphics& g, juce::Rectangle<float> area, const Curve& left, const Curve& right) const
    {
        using namespace DevTestColours;

        const bool running = processor.isSweepRunning();
        const auto fmtHz = [] (float f) { return f < 1000.0f ? juce::String (f, 1) + " Hz" : juce::String (f / 1000.0f, 2) + " kHz"; };
        const auto fmtDb = [] (float db) { return (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; };

        // Line 1: status
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (running || analysing ? accent : text);

        const bool continuous = processor.apvts.getRawParameterValue (ParamIDs::swContinuous)->load() > 0.5f;

        juce::String status;
        if (running && continuous)                   status = "Continuous";
        else if (running)                            status = "Running  " + juce::String (juce::roundToInt (processor.getSweepProgress() * 100.0f)) + " %";
        else if (left.empty())                       status = "Ready - press Start";
        else                                         status = "Finished";

        if (running && ! impulseMode() && ! points.empty())
            status << "     " << fmtHz (points.back().freq);

        if (! impulseMode() && sweepsDone > 0)
            status << "     " << juce::String (sweepsDone) << (sweepsDone == 1 ? " sweep done" : " sweeps done");

        if (impulseMode() && impulsesReceived > 0)
            status << "     " << juce::String (impulsesReceived) << (impulsesReceived == 1 ? " impulse" : " impulses");

        if (impulse != nullptr && impulseMode())
            status << "     Latency " << juce::String (1000.0 * impulse->peak / impulse->sampleRate, 2) << " ms";

        g.drawText (status, area.withHeight (16.0f), juce::Justification::centredLeft);

        // Lines 2 and 3: level at 1 kHz and -3 dB bandwidth per channel
        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& c = ch == 0 ? left : right;
            juce::String line = ch == 0 ? "L" : "R";

            if (! c.empty())
            {
                const float at1k = c.valueAt (1000.0f, std::numeric_limits<float>::quiet_NaN());
                if (! std::isnan (at1k))
                    line << "   Level @ 1 kHz " << fmtDb (at1k);

                float lo = 0.0f, hi = 0.0f;
                bool loFound = false, hiFound = false;
                if (! running && c.findBandwidth (lo, hi, loFound, hiFound))
                    line << "      -3 dB: " << (loFound ? "" : "< ") << fmtHz (lo)
                         << "  ...  " << (hiFound ? "" : "> ") << fmtHz (hi);

            }

            g.setColour (ch == 0 ? traceL : traceR);
            g.drawText (line, area.withY (area.getY() + 18.0f * (float) (ch + 1)).withHeight (16.0f),
                        juce::Justification::centredLeft);
        }
    }

    //==========================================================================
    AudioPluginAudioProcessor& processor;

    std::atomic<float>* modeParam   = nullptr;
    std::atomic<float>* avgParam    = nullptr;
    std::atomic<float>* gateParam   = nullptr;
    std::atomic<float>* smoothParam = nullptr;
    std::atomic<float>* rangeParam  = nullptr;
    std::atomic<float>* normParam   = nullptr;

    std::vector<SweepPoint> points, previousPoints, readBuffer;
    int sweepsDone = 0;

    std::shared_ptr<const ImpulseData>  impulse;
    std::shared_ptr<const SpectrumData> spectrum;
    int  lastGate = -1, lastSmooth = -1;
    int  jobCounter = 0;
    bool analysing = false, spectrumDirty = false;

    // Synchronous average of the received impulse responses
    std::vector<float> averageL, averageR, tempL, tempR;
    uint64_t lastPeriodRead = 0;
    int impulsesReceived = 0;

    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepComponent)
};
