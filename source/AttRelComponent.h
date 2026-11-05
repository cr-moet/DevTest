#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "DevTestLookAndFeel.h"

//==============================================================================
// Attack/release display.
//
// The test sends two sine bursts directly after each other. After the run, the
// amplitude envelopes of the sent and the returned signal are obtained by
// synchronous (I/Q) demodulation at the test frequency, averaged over whole
// periods. Their ratio is the gain of the device over time (in dB), corrected
// for the loop latency measured with a click before the bursts.
//
//  - Attack:  at the start of burst 1 the gain drops from its start value to
//             the compressed value.
//  - Release: at the change to the quieter burst 2 the gain recovers again.
//
// The display can be zoomed with the mouse wheel (around the mouse position),
// moved by dragging and reset with a double-click.
//
// The times are given from the step to 63 % (one time constant) and to 90 % of
// the total gain change. The time resolution is about one period of the test tone.
//==============================================================================
class AttRelComponent final : public juce::Component,
                              private juce::Timer
{
public:
    explicit AttRelComponent (AudioPluginAudioProcessor& p)
        : processor (p)
    {
        setOpaque (false);
        startTimerHz (20);
    }

    // Called before a new test starts
    void clear()
    {
        result = {};
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace DevTestColours;

        drawPlot (g, getPlotArea());
        drawInfo (g, getLocalBounds().toFloat().removeFromTop (94.0f).reduced (12.0f, 2.0f));
    }

    //==========================================================================
    // Zoom (mouse wheel), move (drag), reset (double-click)
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto plot = getPlotArea();
        if (! result.valid || ! plot.contains (e.position) || juce::approximatelyEqual (wheel.deltaY, 0.0f))
            return;

        // Time under the mouse stays in place
        const double frac  = (e.position.x - plot.getX()) / plot.getWidth();
        const double t     = viewT0 + frac * (viewT1 - viewT0);
        const double scale = wheel.deltaY > 0.0f ? 0.8 : 1.25;

        setView (t - (t - viewT0) * scale, t + (viewT1 - t) * scale);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartT0 = viewT0;
        dragStartX  = e.position.x;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! result.valid)
            return;

        const double span  = viewT1 - viewT0;
        const double shift = -(e.position.x - dragStartX) / getPlotArea().getWidth() * span;
        setView (dragStartT0 + shift, dragStartT0 + shift + span);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setView (fullT0, fullT1);
    }

private:
    juce::Rectangle<float> getPlotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop (94.0f)
                                         .withTrimmedLeft (44.0f).withTrimmedRight (16.0f).withTrimmedBottom (20.0f);
    }

    // Sets the visible time range (in samples), limited to the recorded range
    void setView (double t0, double t1)
    {
        const double fullSpan = fullT1 - fullT0;
        const double span = juce::jlimit (32.0, fullSpan, t1 - t0);   // at least 32 samples
        t0 = juce::jlimit (fullT0, fullT1 - span, t0);

        viewT0 = t0;
        viewT1 = t0 + span;
        repaint();
    }

    //==========================================================================
    struct ChannelResult
    {
        std::vector<float> gainDb;          // gain over time (one value per sample, NaN = invalid)

        bool  hasAttack = false, hasRelease = false;
        float attack63 = 0.0f, attack90 = 0.0f, attackChange = 0.0f;     // ms, ms, dB
        float release63 = 0.0f, release90 = 0.0f, releaseChange = 0.0f;  // ms, ms, dB
        juce::String attackNote, releaseNote;
    };

    struct Result
    {
        bool valid = false;
        ChannelResult left, right;
        double sampleRate = 48000.0;
        int64_t burst1Start = 0, burst2Start = 0, burst2End = 0;
        int64_t attackEvent = -1, releaseEvent = -1;   // sample positions of the steps

        // Returned waveform (latency-aligned) and its envelope for the display,
        // from viewStart to viewStart + size
        int64_t viewStart = 0;
        std::vector<float> waveL, waveR, envL, envR;
        float periodMs = 1.0f, latencyMs = 0.0f;
        float amp1Db = 0.0f, amp2Db = 0.0f;
    };

    //==========================================================================
    void timerCallback() override
    {
        const auto runId = processor.getBurstRunId();

        if (! processor.isBurstRunning() && runId != analysedRunId && processor.getBurstRecording().length > 0)
        {
            analysedRunId = runId;
            analyse();
        }

        if (isShowing())
            repaint();
    }

    // Amplitude envelope by I/Q demodulation, moving average over 'window' samples
    static std::vector<float> envelope (const std::vector<float>& x, int64_t offset, int64_t length,
                                        double omega, int window)
    {
        std::vector<double> sumI ((size_t) length + 1, 0.0), sumQ ((size_t) length + 1, 0.0);

        for (int64_t n = 0; n < length; ++n)
        {
            const auto idx = n + offset;
            const double v = (idx >= 0 && idx < (int64_t) x.size()) ? x[(size_t) idx] : 0.0;
            sumI[(size_t) n + 1] = sumI[(size_t) n] + v * std::cos (omega * (double) n);
            sumQ[(size_t) n + 1] = sumQ[(size_t) n] + v * std::sin (omega * (double) n);
        }

        std::vector<float> env ((size_t) length, 0.0f);
        const int half = window / 2;

        for (int64_t n = 0; n < length; ++n)
        {
            const auto a = juce::jlimit ((int64_t) 0, length, n - half);
            const auto b = juce::jlimit ((int64_t) 0, length, a + window);
            const double count = (double) juce::jmax ((int64_t) 1, b - a);
            const double i = (sumI[(size_t) b] - sumI[(size_t) a]) / count;
            const double q = (sumQ[(size_t) b] - sumQ[(size_t) a]) / count;
            env[(size_t) n] = (float) (2.0 * std::sqrt (i * i + q * q));
        }

        return env;
    }

    static float median (std::vector<float> values)
    {
        values.erase (std::remove_if (values.begin(), values.end(), [] (float v) { return std::isnan (v); }), values.end());
        if (values.empty())
            return std::numeric_limits<float>::quiet_NaN();

        std::nth_element (values.begin(), values.begin() + (long) values.size() / 2, values.end());
        return values[values.size() / 2];
    }

    // Time from 'event' until the gain has covered 'fraction' of the change from start to end
    static float timeToFraction (const std::vector<float>& g, int64_t event, int64_t limit,
                                 float start, float end, float fraction, double sr)
    {
        const float change = end - start;
        for (int64_t n = event; n < limit; ++n)
        {
            const float v = g[(size_t) n];
            if (! std::isnan (v) && (v - start) / change >= fraction)
                return (float) (1000.0 * (double) (n - event) / sr);
        }
        return std::numeric_limits<float>::quiet_NaN();
    }

    void analyse()
    {
        const auto& rec = processor.getBurstRecording();
        Result r;

        r.sampleRate  = rec.sampleRate;
        r.burst1Start = rec.burst1Start;
        r.burst2Start = rec.burst2Start;
        r.burst2End   = rec.burst2End;
        r.latencyMs   = (float) (1000.0 * rec.latency / rec.sampleRate);
        r.amp1Db      = juce::Decibels::gainToDecibels (rec.amp1);
        r.amp2Db      = juce::Decibels::gainToDecibels (rec.amp2);

        // Averaging window: whole periods, at least 8 samples
        const double samplesPerPeriod = rec.sampleRate / rec.freq;
        const int periods = juce::jmax (1, (int) std::ceil (8.0 / samplesPerPeriod));
        const int window  = juce::jmax (8, juce::roundToInt (periods * samplesPerPeriod));
        r.periodMs = (float) (1000.0 * window / rec.sampleRate);

        const double omega  = juce::MathConstants<double>::twoPi * rec.freq / rec.sampleRate;
        const auto   length = rec.length;

        // Sent envelope at time n, returned envelope at time n + latency
        const auto envSent = envelope (rec.sent,    0,           length, omega, window);
        const auto envL    = envelope (rec.returnL, rec.latency, length, omega, window);
        const auto envR    = envelope (rec.returnR, rec.latency, length, omega, window);

        const auto gainCurve = [&] (const std::vector<float>& envRet)
        {
            std::vector<float> g ((size_t) length, std::numeric_limits<float>::quiet_NaN());
            const auto from = rec.burst1Start + window / 2, to = rec.burst2End - window / 2;

            for (auto n = from; n < to; ++n)
                if (envSent[(size_t) n] > 1.0e-6f && envRet[(size_t) n] > 1.0e-9f)
                    g[(size_t) n] = 20.0f * std::log10 (envRet[(size_t) n] / envSent[(size_t) n]);
            return g;
        };

        r.left.gainDb  = gainCurve (envL);
        r.right.gainDb = gainCurve (envR);

        // Which step is the attack and which the release
        const bool secondLouder = rec.amp2 > rec.amp1 * 1.01f;
        const bool secondQuieter = rec.amp2 < rec.amp1 * 0.99f;
        r.attackEvent  = secondLouder ? rec.burst2Start : rec.burst1Start;
        r.releaseEvent = secondQuieter ? rec.burst2Start : -1;

        const auto len1 = rec.burst2Start - rec.burst1Start, len2 = rec.burst2End - rec.burst2Start;

        for (auto* ch : { &r.left, &r.right })
        {
            const auto& g = ch->gainDb;
            const auto slice = [&g] (int64_t a, int64_t b) { return std::vector<float> (g.begin() + a, g.begin() + juce::jmax (a, b)); };

            // Settled gain at the end of each burst (median of the last 20 %)
            const float settled1 = median (slice (rec.burst2Start - len1 / 5, rec.burst2Start - window));
            const float settled2 = median (slice (rec.burst2End   - len2 / 5, rec.burst2End   - window));

            // ---- Attack ----
            {
                float start, end;
                int64_t limit;

                if (secondLouder)
                {
                    start = settled1;  end = settled2;  limit = rec.burst2End - window / 2;
                }
                else
                {
                    // From silence: the start value is the highest gain in the first two periods
                    const auto first = slice (rec.burst1Start + window / 2, rec.burst1Start + window / 2 + 2 * window);
                    start = -1000.0f;
                    for (auto v : first) if (! std::isnan (v)) start = juce::jmax (start, v);
                    end = settled1;  limit = rec.burst2Start - window / 2;
                }

                ch->attackChange = end - start;

                if (std::isnan (start) || std::isnan (end) || start < -999.0f)
                    ch->attackNote = "no signal";
                else if (std::abs (end - start) < 0.5f)
                    ch->attackNote = "no gain change (below threshold or faster than " + juce::String (r.periodMs, 1) + " ms)";
                else
                {
                    ch->attack63  = timeToFraction (g, r.attackEvent, limit, start, end, 0.632f, rec.sampleRate);
                    ch->attack90  = timeToFraction (g, r.attackEvent, limit, start, end, 0.9f,   rec.sampleRate);
                    ch->hasAttack = true;
                }
            }

            // ---- Release ----
            if (r.releaseEvent >= 0)
            {
                const float start = settled1, end = settled2;
                ch->releaseChange = end - start;

                if (std::isnan (start) || std::isnan (end))
                    ch->releaseNote = "no signal";
                else if (std::abs (end - start) < 0.5f)
                    ch->releaseNote = "no gain change";
                else
                {
                    const auto limit = rec.burst2End - window / 2;
                    ch->release63  = timeToFraction (g, r.releaseEvent, limit, start, end, 0.632f, rec.sampleRate);
                    ch->release90  = timeToFraction (g, r.releaseEvent, limit, start, end, 0.9f,   rec.sampleRate);
                    ch->hasRelease = true;
                }
            }
            else
            {
                ch->releaseNote = "not measured (burst 2 must be quieter than burst 1)";
            }
        }

        // Display data: 50 ms before burst 1 until 50 ms after burst 2
        r.viewStart = juce::jmax ((int64_t) 0, rec.burst1Start - (int64_t) (0.05 * rec.sampleRate));
        const auto viewEnd = juce::jmin (length, rec.burst2End + (int64_t) (0.05 * rec.sampleRate));

        for (auto n = r.viewStart; n < viewEnd; ++n)
        {
            const auto ret = n + rec.latency;
            const bool ok  = ret < (int64_t) rec.returnL.size();
            r.waveL.push_back (ok ? rec.returnL[(size_t) ret] : 0.0f);
            r.waveR.push_back (ok ? rec.returnR[(size_t) ret] : 0.0f);
            r.envL.push_back (envL[(size_t) n]);
            r.envR.push_back (envR[(size_t) n]);
        }

        r.valid = true;
        result = std::move (r);

        // Show everything after a new measurement
        fullT0 = (double) (result.burst1Start - (int64_t) (0.05 * result.sampleRate));
        fullT1 = (double) (result.burst2End   + (int64_t) (0.05 * result.sampleRate));
        viewT0 = fullT0;
        viewT1 = fullT1;
    }

    //==========================================================================
    // Vertical time lines (ms relative to the start of burst 1) with "nice" steps
    void drawTimeGrid (juce::Graphics& g, juce::Rectangle<float> area, int64_t t0, int64_t t1, bool labels) const
    {
        using namespace DevTestColours;

        const double sr     = result.sampleRate;
        const double spanMs = 1000.0 * (double) (t1 - t0) / sr;

        double step = 0.01;
        for (double s : { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0 })
            if (spanMs / s <= 10.0) { step = s; break; }

        const double startMs = 1000.0 * (double) (t0 - result.burst1Start) / sr;
        const int decimals   = step < 0.1 ? 2 : (step < 1.0 ? 1 : 0);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        for (double ms = std::ceil (startMs / step) * step; ; ms += step)
        {
            if (std::abs (ms) < step * 1.0e-6)
                ms = 0.0;   // avoid "-0 ms"

            const double n = (double) result.burst1Start + ms * 0.001 * sr;
            if (n > (double) t1) break;

            const float x = area.getX() + area.getWidth() * (float) ((n - (double) t0) / (double) (t1 - t0));
            g.setColour (grid);
            g.drawLine (x, area.getY(), x, area.getBottom(), 1.0f);

            if (labels)
            {
                g.setColour (text.withAlpha (0.7f));
                g.drawText (juce::String (ms, decimals) + " ms", juce::Rectangle<float> (x - 34.0f, area.getBottom() + 2.0f, 68.0f, 14.0f),
                            juce::Justification::centred);
            }
        }
    }

    // Returned signal (faint) with its envelope on top, for L and R
    void drawWaveform (juce::Graphics& g, juce::Rectangle<float> area, int64_t t0, int64_t t1) const
    {
        using namespace DevTestColours;

        const auto count = (int64_t) result.waveL.size();
        if (count < 2)
            return;

        // Burst areas
        const auto toX = [&] (int64_t n) { return area.getX() + area.getWidth() * (float) (n - t0) / (float) (t1 - t0); };
        g.setColour (text.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (toX (result.burst1Start), area.getY(), toX (result.burst2Start), area.getBottom()));
        g.setColour (accent.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (toX (result.burst2Start), area.getY(), toX (result.burst2End), area.getBottom()));

        // Common amplitude scale of L and R, fitted to the visible range
        // (so the signal fills the display when zoomed in)
        float peak = 1.0e-6f;
        {
            const auto a = juce::jlimit ((int64_t) 0, count - 1, t0 - result.viewStart);
            const auto b = juce::jlimit ((int64_t) 0, count - 1, t1 - result.viewStart);
            for (auto n = a; n <= b; ++n)
                peak = std::max ({ peak, std::abs (result.waveL[(size_t) n]), std::abs (result.waveR[(size_t) n]),
                                   result.envL[(size_t) n], result.envR[(size_t) n] });
        }
        peak *= 1.1f;
        const auto toY = [&] (float v) { return area.getCentreY() - area.getHeight() * 0.5f * v / peak; };

        g.setColour (grid);
        g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);
        drawTimeGrid (g, area, t0, t1, false);

        const int width = juce::jmax (1, (int) area.getWidth());
        const bool fewSamples = (t1 - t0) < (int64_t) width * 2;   // zoomed in far: draw the samples as a line

        const auto columnRange = [&] (int px)
        {
            const auto a = t0 + (int64_t) ((double) px       / width * (double) (t1 - t0)) - result.viewStart;
            const auto b = t0 + (int64_t) ((double) (px + 1) / width * (double) (t1 - t0)) - result.viewStart;
            return std::make_pair (juce::jlimit ((int64_t) 0, count - 1, a), juce::jlimit ((int64_t) 0, count - 1, juce::jmax (a, b - 1)));
        };

        const auto draw = [&] (const std::vector<float>& wave, const std::vector<float>& env, juce::Colour colour)
        {
            // Waveform: faint in the overview (dense band), increasingly clear the further
            // you zoom in. Drawn as a line through the samples when zoomed in far,
            // otherwise as min/max per pixel column.
            const double samplesPerPixel = (double) (t1 - t0) / (double) width;
            const float  zoomAmount = (float) juce::jlimit (0.0, 1.0, 1.0 - std::log10 (juce::jmax (1.0, samplesPerPixel)) / std::log10 (50.0));
            g.setColour (colour.withAlpha (0.22f + 0.5f * zoomAmount));

            if (fewSamples)
            {
                juce::Path p;
                bool started = false;

                for (auto n = juce::jmax (t0, result.viewStart); n <= t1 && n - result.viewStart < count; ++n)
                {
                    const juce::Point<float> pt (toX (n), toY (wave[(size_t) (n - result.viewStart)]));
                    if (! started) { p.startNewSubPath (pt); started = true; }
                    else           p.lineTo (pt);
                }
                g.strokePath (p, juce::PathStrokeType (1.0f + 0.6f * zoomAmount));

                // Individual samples as dots when there is enough room
                if (samplesPerPixel < 0.15)
                    for (auto n = juce::jmax (t0, result.viewStart); n <= t1 && n - result.viewStart < count; ++n)
                        g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre ({ toX (n), toY (wave[(size_t) (n - result.viewStart)]) }));
            }
            else for (int px = 0; px < width; ++px)
            {
                const auto [a, b] = columnRange (px);
                float mn = 1.0e9f, mx = -1.0e9f;
                for (auto n = a; n <= b; ++n) { mn = juce::jmin (mn, wave[(size_t) n]); mx = juce::jmax (mx, wave[(size_t) n]); }
                const float x = area.getX() + (float) px + 0.5f;
                g.drawLine (x, toY (mx), x, toY (mn), 1.0f);
            }

            // Envelope (+ and -) on top
            juce::Path upper, lower;
            for (int px = 0; px < width; ++px)
            {
                const auto [a, b] = columnRange (px);
                const float e = env[(size_t) ((a + b) / 2)];
                const float x = area.getX() + (float) px + 0.5f;
                if (px == 0) { upper.startNewSubPath (x, toY (e)); lower.startNewSubPath (x, toY (-e)); }
                else         { upper.lineTo (x, toY (e));          lower.lineTo (x, toY (-e)); }
            }
            g.setColour (colour);
            g.strokePath (upper, juce::PathStrokeType (1.6f));
            g.strokePath (lower, juce::PathStrokeType (1.6f));
        };

        draw (result.waveR, result.envR, traceR);
        draw (result.waveL, result.envL, traceL);

        g.setColour (text.withAlpha (0.6f));
        g.drawRect (area, 1.0f);

        // Label on a light background so it stays readable above the waveform
        const juce::String label = "Returned signal + envelope      (scroll: zoom, drag: move, double-click: reset)";
        const auto font = juce::Font (juce::FontOptions (11.0f));
        const float labelWidth = juce::GlyphArrangement::getStringWidth (font, label) + 8.0f;
        const auto labelArea = juce::Rectangle<float> (area.getX() + 1.0f, area.getBottom() - 17.0f, labelWidth, 16.0f);

        g.setColour (background.withAlpha (0.85f));
        g.fillRect (labelArea);
        g.setColour (text.withAlpha (0.8f));
        g.setFont (font);
        g.drawText (label, labelArea.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
    }

    void drawPlot (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace DevTestColours;

        if (! result.valid)
        {
            g.setColour (text.withAlpha (0.6f));
            g.drawRect (plot, 1.0f);
            g.setColour (text.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText ("Gain over time appears here after the test", plot, juce::Justification::centred);
            return;
        }

        const double sr = result.sampleRate;
        const auto t0 = (int64_t) std::floor (viewT0), t1 = (int64_t) std::ceil (viewT1);

        // Upper part: returned waveform with envelope, lower part: gain over time
        auto waveArea = plot.removeFromTop (plot.getHeight() * 0.45f);
        plot.removeFromTop (10.0f);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (waveArea.toNearestInt().expanded (1));
            drawWaveform (g, waveArea, t0, t1);
        }

        g.setColour (text.withAlpha (0.6f));
        g.drawRect (plot, 1.0f);

        // Time grid labels below the gain plot (drawn without clipping)
        drawTimeGrid (g, plot, t0, t1, true);

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt().expanded (60, 8).withTrimmedRight (60));

        const auto toX = [&] (int64_t n) { return plot.getX() + plot.getWidth() * (float) (n - t0) / (float) (t1 - t0); };

        // Value range of both curves (rounded to 3 dB)
        float lo = 1000.0f, hi = -1000.0f;
        for (const auto* ch : { &result.left, &result.right })
            for (auto v : ch->gainDb)
                if (! std::isnan (v)) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }

        if (lo > hi) { lo = -12.0f; hi = 0.0f; }
        lo = std::floor ((lo - 1.5f) / 3.0f) * 3.0f;
        hi = std::ceil  ((hi + 1.5f) / 3.0f) * 3.0f;
        const auto toY = [&] (float db) { return plot.getBottom() - plot.getHeight() * (db - lo) / (hi - lo); };

        // Burst areas (limited to the plot, the clip region also covers the labels left of it)
        const auto band = [&] (int64_t a, int64_t b)
        {
            return juce::Rectangle<float>::leftTopRightBottom (toX (a), plot.getY(), toX (b), plot.getBottom()).getIntersection (plot);
        };
        g.setColour (text.withAlpha (0.05f));
        g.fillRect (band (result.burst1Start, result.burst2Start));
        g.setColour (accent.withAlpha (0.05f));
        g.fillRect (band (result.burst2Start, result.burst2End));

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (text.withAlpha (0.7f));
        const auto burstLabel = [&] (int64_t start, int64_t end, const juce::String& textToShow)
        {
            const float x = juce::jmax (plot.getX(), toX (start));   // keep the label visible when zoomed
            const float w = juce::jmin (200.0f, juce::jmin (toX (end), plot.getRight()) - x - 8.0f);
            if (w >= 100.0f)                                          // only where there is room for it
                g.drawText (textToShow, juce::Rectangle<float> (x + 4.0f, plot.getY() + 2.0f, w, 14.0f), juce::Justification::centredLeft);
        };
        burstLabel (result.burst1Start, result.burst2Start, "Burst 1  " + juce::String (result.amp1Db, 1) + " dBFS");
        burstLabel (result.burst2Start, result.burst2End,   "Burst 2  " + juce::String (result.amp2Db, 1) + " dBFS");

        // Level grid (dB)
        const float step = (hi - lo) > 24.0f ? 6.0f : 3.0f;
        for (float db = lo; db <= hi + 0.01f; db += step)
        {
            const float y = toY (db);
            g.setColour (grid);
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f);
            g.setColour (text.withAlpha (0.7f));
            g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 40.0f, y - 7.0f, 36.0f, 14.0f),
                        juce::Justification::centredRight);
        }

        // Gain curves (min/max per pixel column)
        const auto drawCurve = [&] (const ChannelResult& ch, juce::Colour colour)
        {
            g.setColour (colour);
            const int width = (int) plot.getWidth();

            for (int px = 0; px < width; ++px)
            {
                const auto a = t0 + (int64_t) ((double) px       / width * (double) (t1 - t0));
                const auto b = t0 + (int64_t) ((double) (px + 1) / width * (double) (t1 - t0));
                float mn = 1000.0f, mx = -1000.0f;

                for (auto n = juce::jmax ((int64_t) 0, a); n <= juce::jmin ((int64_t) ch.gainDb.size() - 1, b); ++n)
                {
                    const float v = ch.gainDb[(size_t) n];
                    if (! std::isnan (v)) { mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
                }

                if (mn <= mx)
                {
                    const float x = plot.getX() + (float) px + 0.5f;
                    g.drawLine (x, toY (mx) - 0.8f, x, toY (mn) + 0.8f, 1.6f);
                }
            }

            // Markers at the 63 % points
            const auto marker = [&] (bool has, int64_t event, float ms)
            {
                if (! has || std::isnan (ms))
                    return;
                const auto n = event + (int64_t) (ms * 0.001 * sr);
                if (n < 0 || n >= (int64_t) ch.gainDb.size() || std::isnan (ch.gainDb[(size_t) n]))
                    return;
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ toX (n), toY (ch.gainDb[(size_t) n]) }));
            };
            marker (ch.hasAttack,  result.attackEvent,  ch.attack63);
            marker (ch.hasRelease, result.releaseEvent, ch.release63);
        };

        drawCurve (result.right, traceR);
        drawCurve (result.left,  traceL);

        g.setColour (text.withAlpha (0.8f));
        g.drawText ("Gain (dB)", juce::Rectangle<float> (plot.getX() + 4.0f, plot.getBottom() - 16.0f, 120.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    void drawInfo (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        using namespace DevTestColours;

        const bool running = processor.isBurstRunning();
        const auto fmtMs = [] (float ms) { return std::isnan (ms) ? juce::String ("--") : juce::String (ms, ms < 10.0f ? 2 : 1) + " ms"; };

        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (running ? accent : text);

        juce::String status;
        if (running)              status = "Running  " + juce::String (juce::roundToInt (processor.getBurstProgress() * 100.0f)) + " %";
        else if (! result.valid)  status = "Ready - press Start";
        else                      status = "Finished";

        if (result.valid && ! running)
            status << "     Latency " << juce::String (result.latencyMs, 2) << " ms     Resolution ~" << juce::String (result.periodMs, 1) << " ms";

        g.drawText (status, area.withHeight (16.0f), juce::Justification::centredLeft);

        if (! result.valid || running)
            return;

        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        for (int c = 0; c < 2; ++c)
        {
            const auto& ch = c == 0 ? result.left : result.right;
            g.setColour (c == 0 ? traceL : traceR);

            juce::String line = c == 0 ? "L   " : "R   ";
            line << "Attack  " << (ch.hasAttack ? "63 %: " + fmtMs (ch.attack63) + "   90 %: " + fmtMs (ch.attack90)
                                                      + "   (GR " + juce::String (-ch.attackChange, 1) + " dB)"
                                                : ch.attackNote);
            g.drawText (line, area.withY (area.getY() + 18.0f + 36.0f * (float) c).withHeight (16.0f), juce::Justification::centredLeft);

            juce::String line2 = "     Release ";
            line2 << (ch.hasRelease ? "63 %: " + fmtMs (ch.release63) + "   90 %: " + fmtMs (ch.release90)
                                          + "   (recovers " + juce::String (ch.releaseChange, 1) + " dB)"
                                    : ch.releaseNote);
            g.drawText (line2, area.withY (area.getY() + 36.0f + 36.0f * (float) c).withHeight (16.0f), juce::Justification::centredLeft);
        }
    }

    //==========================================================================
    AudioPluginAudioProcessor& processor;
    Result   result;
    uint32_t analysedRunId = 0;

    // Visible time range in samples (zoom), full range, drag start
    double viewT0 = 0.0, viewT1 = 1.0, fullT0 = 0.0, fullT1 = 1.0;
    double dragStartT0 = 0.0;
    float  dragStartX = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttRelComponent)
};
