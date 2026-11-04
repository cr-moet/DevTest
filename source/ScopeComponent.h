#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "DevTestLookAndFeel.h"

//==============================================================================
// Oszilloskop-Anzeige: 10 Divisionen horizontal, 8 vertikal, +/-1.0 = 0 dBFS.
// Liest die Samples aus dem Ringpuffer des Processors (60 Bilder/s).
//==============================================================================
class ScopeComponent final : public juce::Component,
                             private juce::Timer
{
public:
    static constexpr int numDivsX = 10;
    static constexpr int numDivsY = 8;

    explicit ScopeComponent (AudioPluginAudioProcessor& p)
        : processor (p)
    {
        timePerDivParam = processor.apvts.getRawParameterValue (ParamIDs::timePerDiv);
        triggerParam    = processor.apvts.getRawParameterValue (ParamIDs::trigger);
        srcLParam       = processor.apvts.getRawParameterValue (ParamIDs::scopeSourceL);
        srcRParam       = processor.apvts.getRawParameterValue (ParamIDs::scopeSourceR);

        setOpaque (true);
        startTimerHz (60);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace DevTestColours;

        g.fillAll (background);

        const auto plot = getPlotArea();
        drawGrid (g, plot);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());

            if (showR) drawTrace (g, plot, samplesR, traceR);
            if (showL) drawTrace (g, plot, samplesL, traceL);
        }

        g.setColour (text.withAlpha (0.6f));
        g.drawRect (plot, 1.0f);

        drawInfo (g, plot);
    }

private:
    //==========================================================================
    juce::Rectangle<float> getPlotArea() const
    {
        return getLocalBounds().toFloat().reduced (36.0f, 24.0f).withTrimmedBottom (2.0f);
    }

    void timerCallback() override
    {
        const double sr      = processor.getCurrentSampleRate();
        const double msPerDiv = (double) timePerDivParam->load();
        const int windowLen  = juce::jlimit (2, ScopeBuffer::size / 2,
                                             (int) std::round (msPerDiv * 0.001 * numDivsX * sr));

        showL = juce::roundToInt (srcLParam->load()) != (int) ScopeSource::off;
        showR = juce::roundToInt (srcRParam->load()) != (int) ScopeSource::off;

        const auto& buf     = processor.scopeBuffer;
        const auto writePos = buf.getWritePos();

        if (writePos < (uint64_t) windowLen + 4)
            return; // noch nicht genug Daten

        // ---------------- Pegelmessung (letzte 300 ms) ----------------
        {
            const auto measureLen = std::min<uint64_t> (writePos, (uint64_t) std::max (1.0, sr * 0.3));
            double sumL = 0.0, sumR = 0.0;
            float peakL = 0.0f, peakR = 0.0f;

            for (uint64_t idx = writePos - measureLen; idx < writePos; ++idx)
            {
                const float l = buf.getL (idx), r = buf.getR (idx);
                peakL = std::max (peakL, std::abs (l));
                peakR = std::max (peakR, std::abs (r));
                sumL += (double) l * l;
                sumR += (double) r * r;
            }

            levelPeakL = juce::Decibels::gainToDecibels (peakL, -120.0f);
            levelPeakR = juce::Decibels::gainToDecibels (peakR, -120.0f);
            levelRmsL  = juce::Decibels::gainToDecibels ((float) std::sqrt (sumL / (double) measureLen), -120.0f);
            levelRmsR  = juce::Decibels::gainToDecibels ((float) std::sqrt (sumR / (double) measureLen), -120.0f);
        }

        // ---------------- Trigger ----------------
        // Startpunkt ohne Trigger: einfach die neuesten Samples
        uint64_t start = writePos - (uint64_t) windowLen - 2;
        fracOffset = 0.0f;
        triggered = false;

        if (triggerParam->load() > 0.5f && (showL || showR) && writePos > (uint64_t) windowLen + 2)
        {
            const bool useL = showL;
            const auto get  = [&buf, useL] (uint64_t i) { return useL ? buf.getL (i) : buf.getR (i); };

            // Suchbereich: mindestens 100 ms bzw. eine Fensterbreite zurueck
            const auto available  = std::min<uint64_t> (writePos, (uint64_t) ScopeBuffer::size - 1);
            const auto latestIdx  = writePos - (uint64_t) windowLen - 1;
            const auto searchLen  = std::min<uint64_t> (available - (uint64_t) windowLen - 2,
                                                        (uint64_t) std::max (sr * 0.1, (double) windowLen));
            const auto searchFrom = latestIdx - searchLen;

            // Hysterese passend zum Signalpegel (5 % vom Spitzenwert), damit Rauschen
            // am Nulldurchgang keine Fehltrigger ausloest
            float peak = 0.0f;
            for (uint64_t i = searchFrom; i <= latestIdx; ++i)
                peak = std::max (peak, std::abs (get (i)));

            const float hysteresis = std::max (0.0005f, peak * 0.05f);

            // Schmitt-Trigger vorwaerts: "scharf" unterhalb -Hysterese,
            // ausloesen beim naechsten Durchgang durch 0 nach oben.
            bool armed = false;
            for (uint64_t i = searchFrom + 1; i <= latestIdx; ++i)
            {
                const float prev = get (i - 1);
                const float cur  = get (i);

                if (cur < -hysteresis)
                    armed = true;

                if (armed && prev < 0.0f && cur >= 0.0f)
                {
                    // Nulldurchgang liegt zwischen i-1 und i: genau interpolieren,
                    // damit das Bild nicht um ein ganzes Sample hin- und herspringt
                    start      = i - 1;
                    fracOffset = prev / (prev - cur);
                    triggered  = true;
                    armed      = false;
                }
            }
        }

        // windowLen + 2 Samples, damit das Bild trotz Subsample-Versatz bis zum Rand reicht
        samplesL.resize ((size_t) windowLen + 2);
        samplesR.resize ((size_t) windowLen + 2);

        for (int i = 0; i < windowLen + 2; ++i)
        {
            samplesL[(size_t) i] = buf.getL (start + (uint64_t) i);
            samplesR[(size_t) i] = buf.getR (start + (uint64_t) i);
        }

        currentWindowLen = windowLen;

        currentMsPerDiv = msPerDiv;
        currentSampleRate = sr;
        repaint();
    }

    //==========================================================================
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace DevTestColours;

        const float dx = plot.getWidth()  / (float) numDivsX;
        const float dy = plot.getHeight() / (float) numDivsY;

        for (int i = 1; i < numDivsX; ++i)
        {
            const float x = plot.getX() + dx * (float) i;
            g.setColour (i == numDivsX / 2 ? gridStrong : grid);
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);
        }

        for (int i = 1; i < numDivsY; ++i)
        {
            const float y = plot.getY() + dy * (float) i;
            g.setColour (i == numDivsY / 2 ? gridStrong : grid);
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f);
        }

        // Feine Teilstriche auf den Mittelachsen
        g.setColour (gridStrong);
        const float cx = plot.getCentreX(), cy = plot.getCentreY();

        for (int i = 0; i <= numDivsX * 5; ++i)
        {
            const float x = plot.getX() + dx * 0.2f * (float) i;
            g.drawLine (x, cy - 3.0f, x, cy + 3.0f, 1.0f);
        }

        for (int i = 0; i <= numDivsY * 5; ++i)
        {
            const float y = plot.getY() + dy * 0.2f * (float) i;
            g.drawLine (cx - 3.0f, y, cx + 3.0f, y, 1.0f);
        }

        // Amplitudenbeschriftung links
        g.setColour (text.withAlpha (0.7f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        for (int i = 0; i <= numDivsY; i += 2)
        {
            const float value = 1.0f - 2.0f * (float) i / (float) numDivsY;
            const float y     = plot.getY() + dy * (float) i;
            g.drawText (juce::String (value, 1),
                        juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 4.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }

    void drawTrace (juce::Graphics& g, juce::Rectangle<float> plot,
                    const std::vector<float>& data, juce::Colour colour) const
    {
        const int windowLen = currentWindowLen;
        if (windowLen < 2 || (int) data.size() < windowLen + 2)
            return;

        const auto toY = [&plot] (float v)
        {
            return plot.getCentreY() - v * plot.getHeight() * 0.5f;
        };

        g.setColour (colour);

        const int width = juce::jmax (1, (int) plot.getWidth());

        if (windowLen <= width * 2)
        {
            // Wenige Samples: Linie durch alle Punkte, um den Subsample-Versatz
            // des Triggers verschoben -> ruhiges Bild
            juce::Path path;
            const float samplesToX = plot.getWidth() / (float) windowLen;

            for (int i = 0; i < windowLen + 2; ++i)
            {
                const float x = plot.getX() + ((float) i - fracOffset) * samplesToX;
                const float y = toY (data[(size_t) i]);
                if (i == 0) path.startNewSubPath (x, y);
                else        path.lineTo (x, y);
            }
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
        }
        else
        {
            // Viele Samples: pro Pixelspalte Min/Max zeichnen (zeigt auch Spitzen korrekt)
            const int n = windowLen;
            float lastValue = data[0];

            for (int px = 0; px < width; ++px)
            {
                const int from = (int) ((int64_t) px       * n / width);
                const int to   = (int) ((int64_t) (px + 1) * n / width);

                float lo = lastValue, hi = lastValue;
                for (int i = from; i < to; ++i)
                {
                    lo = std::min (lo, data[(size_t) i]);
                    hi = std::max (hi, data[(size_t) i]);
                }
                if (to > from)
                    lastValue = data[(size_t) (to - 1)];

                const float x = plot.getX() + (float) px + 0.5f;
                g.drawLine (x, toY (hi) - 0.8f, x, toY (lo) + 0.8f, 1.4f);
            }
        }
    }

    void drawInfo (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        using namespace DevTestColours;

        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));

        const float y = plot.getY() - 20.0f;
        g.setColour (showL ? traceL : traceL.withAlpha (0.25f));
        g.drawText ("L", juce::Rectangle<float> (plot.getX(), y, 20.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (showR ? traceR : traceR.withAlpha (0.25f));
        g.drawText ("R", juce::Rectangle<float> (plot.getX() + 22.0f, y, 20.0f, 16.0f), juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.setColour (text);

        juce::String timeText = currentMsPerDiv < 1.0
                                    ? juce::String (currentMsPerDiv * 1000.0, 0) + " us/Div"
                                    : juce::String (currentMsPerDiv, 2) + " ms/Div";

        g.drawText (timeText + "   |   " + juce::String (currentSampleRate / 1000.0, 1) + " kHz",
                    juce::Rectangle<float> (plot.getX() + 60.0f, y, plot.getWidth() - 160.0f, 16.0f),
                    juce::Justification::centred);

        // Pegelanzeige unter dem Raster
        const auto fmt = [] (float db) { return db <= -119.0f ? juce::String ("-inf") : juce::String (db, 1); };
        const float yb = plot.getBottom() + 4.0f;
        const float half = plot.getWidth() * 0.5f;

        g.setColour (showL ? traceL : traceL.withAlpha (0.3f));
        g.drawText ("L   Peak " + fmt (levelPeakL) + " dBFS   RMS " + fmt (levelRmsL) + " dBFS",
                    juce::Rectangle<float> (plot.getX(), yb, half, 16.0f), juce::Justification::centredLeft);

        g.setColour (showR ? traceR : traceR.withAlpha (0.3f));
        g.drawText ("R   Peak " + fmt (levelPeakR) + " dBFS   RMS " + fmt (levelRmsR) + " dBFS",
                    juce::Rectangle<float> (plot.getX() + half, yb, half, 16.0f), juce::Justification::centredRight);

        g.setColour (triggered ? accent : text.withAlpha (0.4f));
        g.drawText (triggered ? "TRIG" : "FREI",
                    juce::Rectangle<float> (plot.getRight() - 60.0f, y, 60.0f, 16.0f),
                    juce::Justification::centredRight);
    }

    //==========================================================================
    AudioPluginAudioProcessor& processor;

    std::atomic<float>* timePerDivParam = nullptr;
    std::atomic<float>* triggerParam    = nullptr;
    std::atomic<float>* srcLParam       = nullptr;
    std::atomic<float>* srcRParam       = nullptr;

    std::vector<float> samplesL, samplesR;
    bool showL = true, showR = true, triggered = false;
    double currentMsPerDiv = 2.0, currentSampleRate = 48000.0;
    int   currentWindowLen = 0;
    float fracOffset = 0.0f;
    float levelPeakL = -120.0f, levelPeakR = -120.0f, levelRmsL = -120.0f, levelRmsR = -120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
