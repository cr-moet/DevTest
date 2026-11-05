#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "DevTestLookAndFeel.h"

//==============================================================================
// Knee test display: output level plotted against input level (both in dBFS),
// for inputs L and R at the same time (L in the L colour, R in the R colour).
// The dashed diagonal is the 1:1 line of a device in bypass.
// Both axes use the same dB-per-pixel scale (square plot), so ratios can be
// read directly. The values and results are listed to the right of the plot.
// After a finished sweep, threshold, knee width and ratio are estimated for each channel.
//==============================================================================
class KneeComponent final : public juce::Component,
                            private juce::Timer
{
public:
    explicit KneeComponent (AudioPluginAudioProcessor& p)
        : processor (p)
    {
        readBuffer.resize (4096);
        setOpaque (false);
        startTimerHz (30);
    }

    // Called before a new test starts
    void clear()
    {
        points.clear();
        resultL = {};
        resultR = {};
        needsAnalysis = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace DevTestColours;

        const auto plot = getPlotArea();
        drawGrid (g, plot);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());

            // 1:1 reference line (bypass)
            juce::Path diagonal;
            // (in dBu: real unity gain from the calibrated output to the calibrated input)
            diagonal.startNewSubPath (toView (xLo(), xLo(), plot));
            diagonal.lineTo (toView (xHi(), xHi(), plot));

            juce::Path dashed;
            const float dashes[] = { 6.0f, 5.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, diagonal, dashes, 2);
            g.setColour (text.withAlpha (0.45f));
            g.fillPath (dashed);

            // Knee region (shaded) and threshold line per channel
            for (auto* r : { &resultL, &resultR })
            {
                if (r->valid && r->hasKnee)
                {
                    const auto colour = r == &resultL ? traceL : traceR;
                    const float t  = r->thresholdDb + offX();
                    const float x0 = toView (t - r->kneeWidthDb * 0.5f, yLo(), plot).x;
                    const float x1 = toView (t + r->kneeWidthDb * 0.5f, yLo(), plot).x;
                    const float xt = toView (t, yLo(), plot).x;

                    g.setColour (colour.withAlpha (0.07f));
                    g.fillRect (juce::Rectangle<float> (x0, plot.getY(), juce::jmax (0.0f, x1 - x0), plot.getHeight()));

                    g.setColour (colour.withAlpha (0.35f));
                    g.drawLine (xt, plot.getY(), xt, plot.getBottom(), 1.0f);
                }
            }

            // Measured curves: R first, so L lies on top when both are identical
            drawCurve (g, plot, false, traceR);
            drawCurve (g, plot, true,  traceL);
        }

        g.setColour (text.withAlpha (0.6f));
        g.drawRect (plot, 1.0f);

        drawInfo (g, getInfoArea (plot));
    }

private:
    // Per-channel evaluation of the curve
    struct Result
    {
        bool  valid = false;
        float referenceGain = 0.0f;       // gain below the threshold (dB)
        float slopeTop = 1.0f;            // slope of the top 10 dB -> ratio = 1 / slope
        float maxGainReduction = 0.0f;    // dB

        bool  hasKnee = false;
        float thresholdDb = 0.0f;         // knee centre (the "threshold" set on a compressor)
        float kneeWidthDb = 0.0f;         // total knee width, 0 = hard knee

        bool  hasGr1 = false;
        float gr1Db = 0.0f;               // input level where gain reduction reaches 1 dB
    };

    // Axis ranges: input and output both -60..0 dBFS
    static constexpr float minDb    = kneeStartDb;
    static constexpr float maxInDb  = kneeEndDb;
    static constexpr float maxOutDb = kneeEndDb;

    static constexpr float infoWidth = 180.0f;   // text column right of the plot

    static float outOf (const KneePoint& p, bool left)   { return left ? p.outputDbL : p.outputDbR; }

    // Square plot on the left, as large as the height allows
    juce::Rectangle<float> getPlotArea() const
    {
        const auto area = getLocalBounds().toFloat().withTrimmedLeft (52.0f).withTrimmedTop (8.0f)
                                                    .withTrimmedBottom (36.0f).withTrimmedRight (infoWidth + 24.0f);

        const float side = juce::jmax (100.0f, juce::jmin (area.getWidth(), area.getHeight()));
        return { area.getX(), area.getY(), side, side };
    }

    // Text column to the right of the plot
    juce::Rectangle<float> getInfoArea (juce::Rectangle<float> plot) const
    {
        return juce::Rectangle<float> (plot.getRight() + 24.0f, plot.getY(),
                                       (float) getWidth() - plot.getRight() - 28.0f, plot.getHeight());
    }

    // Display units: dBFS, or dBu with the calibration. The sent level (x) uses the
    // calibration of the selected output (L for "L + R"), the returned levels (y)
    // that of input L or R.
    float offX() const
    {
        const int choice = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIDs::kneeOutput)->load());
        return processor.outputOffsetDb (choice == 1 ? 1 : 0);
    }
    float offY (bool left) const  { return processor.inputOffsetDb (left ? 0 : 1); }

    // Axis ranges in display units (always 60 dB wide). Both axes use the range of
    // the sent level, so the 1:1 line (unity gain) is always the diagonal of the plot,
    // also in dBu when inputs and outputs have different full-scale levels.
    float xLo() const  { return minDb    + offX(); }
    float xHi() const  { return maxInDb  + offX(); }
    float yLo() const  { return minDb    + offX(); }
    float yHi() const  { return maxOutDb + offX(); }

    // Point in display units -> screen
    juce::Point<float> toView (float inLevel, float outLevel, juce::Rectangle<float> plot) const
    {
        const float x = plot.getX() + plot.getWidth() * (inLevel - xLo()) / (xHi() - xLo());
        const float y = plot.getBottom() - plot.getHeight() * (juce::jlimit (yLo() - 20.0f, yHi() + 20.0f, outLevel) - yLo())
                                               / (yHi() - yLo());
        return { x, y };
    }

    //==========================================================================
    void timerCallback() override
    {
        if (! isShowing())
            return; // other tab selected -> leave the points in the FIFO

        const auto runId = processor.getKneeRunId();
        bool changed = false;

        for (;;)
        {
            const int n = processor.readKneePoints (readBuffer.data(), (int) readBuffer.size());
            if (n <= 0)
                break;

            for (int i = 0; i < n; ++i)
                if (readBuffer[(size_t) i].runId == runId)   // ignore points of older runs
                    points.push_back (readBuffer[(size_t) i]);

            changed = true;
            needsAnalysis = true;
        }

        const bool running = processor.isKneeTestRunning();

        // Evaluate the curves once the sweep has finished (or was stopped)
        if (! running && needsAnalysis)
        {
            resultL = analyse (true);
            resultR = analyse (false);
            needsAnalysis = false;
        }

        if (changed || running)
            repaint();
    }

    // Estimates threshold and ratio of one channel
    Result analyse (bool left) const
    {
        Result r;

        if (points.size() < 40)
            return r;

        // The first points are skipped for safety (the processor already has a 500 ms
        // lead-in before measuring, so they are normally fine)
        constexpr size_t skip = 2;    // 2 x 20 ms

        // Reference gain: median gain over the first 15 dB of the sweep
        // (usually below the threshold of the device)
        std::vector<float> gains;
        for (size_t i = skip; i < points.size(); ++i)
            if (points[i].inputDb <= minDb + 15.0f)
                gains.push_back (outOf (points[i], left) - points[i].inputDb);

        if (gains.empty())
            return r;

        std::nth_element (gains.begin(), gains.begin() + (long) gains.size() / 2, gains.end());
        r.referenceGain = gains[gains.size() / 2];

        const auto gainAt = [&] (size_t i) { return outOf (points[i], left) - points[i].inputDb; };

        // 1 dB GR point: start of the continuous region at the top of the sweep in which
        // the gain stays at least 1 dB below the reference gain
        const auto isReduced = [&] (size_t i) { return gainAt (i) < r.referenceGain - 1.0f; };

        if (isReduced (points.size() - 1))
        {
            size_t first = points.size() - 1;
            while (first > skip && isReduced (first - 1))
                --first;

            r.gr1Db  = points[first].inputDb;
            r.hasGr1 = true;
        }

        // Upper line: linear regression over the top 10 dB of the curve (y = a + b x)
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for (const auto& p : points)
        {
            if (p.inputDb >= maxInDb - 10.0f)
            {
                const double x = p.inputDb, y = outOf (p, left);
                sx += x;  sy += y;  sxx += x * x;  sxy += x * y;
                ++n;
            }
        }

        const double denom = n * sxx - sx * sx;
        const bool   lineOk = n >= 5 && denom > 1.0e-9;
        const double b = lineOk ? (n * sxy - sx * sy) / denom : 1.0;
        const double a = lineOk ? (sy - b * sx) / n : 0.0;
        r.slopeTop = (float) b;

        // Threshold and knee width from the standard (quadratic) soft-knee model:
        //  - the threshold T is where the upper line meets the 1:1 line (y = x + reference gain)
        //  - at T, a soft knee of width W reduces the gain by (1 - 1/ratio) * W / 8
        //    -> W = 8 * GR(T) / (1 - slope).  A hard knee has GR(T) = 0 -> W = 0.
        if (lineOk && b < 0.95)
        {
            const double t = (a - r.referenceGain) / (1.0 - b);

            if (t > minDb && t < maxInDb)
            {
                // Gain at T, interpolated between the neighbouring points
                size_t i = skip + 1;
                while (i < points.size() - 1 && points[i].inputDb < (float) t)
                    ++i;

                const float x0 = points[i - 1].inputDb, x1 = points[i].inputDb;
                const float frac  = x1 > x0 ? juce::jlimit (0.0f, 1.0f, ((float) t - x0) / (x1 - x0)) : 0.0f;
                const float gainT = gainAt (i - 1) + frac * (gainAt (i) - gainAt (i - 1));
                const float grT   = juce::jmax (0.0f, r.referenceGain - gainT);

                r.thresholdDb = (float) t;
                r.kneeWidthDb = (float) (8.0 * grT / (1.0 - b));
                r.hasKnee     = true;
            }
        }

        for (size_t i = skip; i < points.size(); ++i)
            r.maxGainReduction = juce::jmax (r.maxGainReduction, r.referenceGain - (outOf (points[i], left) - points[i].inputDb));

        r.valid = true;
        return r;
    }

    //==========================================================================
    void drawCurve (juce::Graphics& g, juce::Rectangle<float> plot, bool left, juce::Colour colour) const
    {
        if (points.size() < 2)
            return;

        const float ox = offX(), oy = offY (left);
        const auto view = [&] (const KneePoint& p) { return toView (p.inputDb + ox, outOf (p, left) + oy, plot); };

        juce::Path curve;
        curve.startNewSubPath (view (points[0]));
        for (size_t i = 1; i < points.size(); ++i)
            curve.lineTo (view (points[i]));

        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

        const auto last = view (points.back());
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (last));
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace DevTestColours;

        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        const float x0 = xLo(), x1 = xHi(), y0 = yLo(), y1 = yHi();
        const auto xPos = [&] (float v) { return toView (v, y0, plot).x; };
        const auto yPos = [&] (float v) { return toView (x0, v, plot).y; };

        // Fine dashed grid in 1 dB steps (the 10 dB lines are drawn on top).
        // Each 1 dB line gets one dash per 1 dB cell, centred between the crossings,
        // so the dashes of horizontal and vertical lines do not form diagonal patterns.
        // In dBu the lines lie on whole dBu values, so the cells at the edges can be smaller.
        {
            std::vector<float> xCross { plot.getX() }, yCross { plot.getY() };
            std::vector<float> xFine, yFine;

            for (int v = (int) std::ceil (x0 + 0.01f); (float) v < x1 - 0.01f; ++v)
            {
                xCross.push_back (xPos ((float) v));
                if (v % 10 != 0) xFine.push_back (xPos ((float) v));
            }
            for (int v = (int) std::floor (y1 - 0.01f); (float) v > y0 + 0.01f; --v)   // top to bottom
            {
                yCross.push_back (yPos ((float) v));
                if (v % 10 != 0) yFine.push_back (yPos ((float) v));
            }
            xCross.push_back (plot.getRight());
            yCross.push_back (plot.getBottom());

            juce::Path fine;
            for (auto x : xFine)
                for (size_t k = 0; k + 1 < yCross.size(); ++k)
                {
                    const float a = yCross[k], b = yCross[k + 1];
                    fine.startNewSubPath (x, a + 0.3f * (b - a));  fine.lineTo (x, a + 0.7f * (b - a));
                }
            for (auto y : yFine)
                for (size_t k = 0; k + 1 < xCross.size(); ++k)
                {
                    const float a = xCross[k], b = xCross[k + 1];
                    fine.startNewSubPath (a + 0.3f * (b - a), y);  fine.lineTo (a + 0.7f * (b - a), y);
                }

            g.setColour (text.withAlpha (0.16f));
            g.strokePath (fine, juce::PathStrokeType (0.7f));
        }

        const bool dbu = processor.isUnitDbu();
        const auto label = [] (int db) { return (db > 0 ? "+" : "") + juce::String (db); };

        // Vertical lines: input level (every 10 dB)
        for (int db = (int) std::ceil (x0 / 10.0f - 0.001f) * 10; (float) db <= x1 + 0.001f; db += 10)
        {
            const float x = xPos ((float) db);
            g.setColour (! dbu && db == 0 ? gridStrong : grid);
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);

            g.setColour (text.withAlpha (0.7f));
            g.drawText (label (db), juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f),
                        juce::Justification::centred);
        }

        // Horizontal lines: output level
        for (int db = (int) std::ceil (y0 / 10.0f - 0.001f) * 10; (float) db <= y1 + 0.001f; db += 10)
        {
            const float y = yPos ((float) db);
            g.setColour (! dbu && db == 0 ? gridStrong : grid);
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f);

            g.setColour (text.withAlpha (0.7f));
            g.drawText (label (db),
                        juce::Rectangle<float> (plot.getX() - 40.0f, y - 7.0f, 36.0f, 14.0f),
                        juce::Justification::centredRight);
        }

        // Axis titles
        g.setColour (text);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        const auto unit = processor.getLevelUnit();
        g.drawText ("Input (" + unit + ")", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 16.0f, plot.getWidth(), 16.0f),
                    juce::Justification::centred);

        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                         plot.getX() - 46.0f, plot.getCentreY()));
        g.drawText ("Output (" + unit + ")", juce::Rectangle<float> (plot.getX() - 46.0f - 60.0f, plot.getCentreY() - 8.0f, 120.0f, 16.0f),
                    juce::Justification::centred);
    }

    void drawInfo (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        using namespace DevTestColours;

        const bool running = processor.isKneeTestRunning();
        const auto fmt = [] (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1); };

        float y = area.getY();
        const auto lineOut = [&] (const juce::String& textLine, float spacingAfter = 18.0f)
        {
            g.drawText (textLine, juce::Rectangle<float> (area.getX(), y, area.getWidth(), 16.0f),
                        juce::Justification::centredLeft);
            y += spacingAfter;
        };

        // Label in the first column, value in the second
        const auto valueOut = [&] (const juce::String& label, const juce::String& value, float spacingAfter = 18.0f)
        {
            g.drawText (label, juce::Rectangle<float> (area.getX(), y, 74.0f, 16.0f), juce::Justification::centredLeft);
            g.drawText (value, juce::Rectangle<float> (area.getX() + 74.0f, y, juce::jmax (0.0f, area.getWidth() - 74.0f), 16.0f),
                        juce::Justification::centredLeft);
            y += spacingAfter;
        };

        // Status and current input level
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (running ? accent : text);

        if (running)              lineOut ("Running  " + juce::String (juce::roundToInt (processor.getKneeProgress() * 100.0f)) + " %");
        else if (points.empty())  lineOut ("Ready - press START");
        else                      lineOut ("Finished");

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.setColour (text);
        const auto unit = " " + processor.getLevelUnit();
        const float ox  = offX();
        valueOut ("Input", points.empty() ? juce::String ("--") : fmt (points.back().inputDb + ox) + unit, 34.0f);

        // One block per channel
        for (int ch = 0; ch < 2; ++ch)
        {
            const bool left = ch == 0;
            const auto& r   = left ? resultL : resultR;
            g.setColour (left ? traceL : traceR);

            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            lineOut (left ? "Channel L" : "Channel R");

            g.setFont (juce::Font (juce::FontOptions (12.0f)));

            if (! points.empty())
            {
                // In dBu the gain is the real gain of the device under test
                const float out = outOf (points.back(), left) + offY (left);
                valueOut ("Output", fmt (out) + unit);
                valueOut ("Gain",   fmt (out - (points.back().inputDb + ox)) + " dB");
            }

            if (r.valid && ! running)
            {
                const float ratio = r.slopeTop > 0.02f ? 1.0f / r.slopeTop : 0.0f;
                valueOut ("Threshold", r.hasKnee ? fmt (r.thresholdDb + ox) + unit : juce::String ("--"));
                valueOut ("Knee",      ! r.hasKnee ? juce::String ("--")
                                         : r.kneeWidthDb < 0.5f ? juce::String ("hard")
                                                                : juce::String (r.kneeWidthDb, 1) + " dB (soft)");
                valueOut ("Ratio",     ratio > 0.0f ? juce::String (ratio, 1) + ":1" : juce::String ("inf:1"));
                valueOut ("1 dB GR",   r.hasGr1 ? fmt (r.gr1Db + ox) + unit : juce::String ("--"));
                valueOut ("Max GR",    juce::String (r.maxGainReduction, 1) + " dB");
            }

            y += 16.0f;
        }

        // How the results are defined
        g.setColour (text.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText ("Ratio = slope of the top 10 dB.\nThreshold = knee centre, knee = total\nwidth (soft-knee model, shaded).",
                          juce::Rectangle<float> (area.getX(), area.getBottom() - 44.0f, area.getWidth(), 44.0f).toNearestInt(),
                          juce::Justification::bottomLeft, 3);
    }

    //==========================================================================
    AudioPluginAudioProcessor& processor;

    std::vector<KneePoint> points, readBuffer;
    bool needsAnalysis = false;
    Result resultL, resultR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KneeComponent)
};
