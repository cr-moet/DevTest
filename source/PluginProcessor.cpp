#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor (makeBusesProperties()),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    scopeSrcLParam   = apvts.getRawParameterValue (ParamIDs::scopeSourceL);
    scopeSrcRParam   = apvts.getRawParameterValue (ParamIDs::scopeSourceR);
    passThroughParam = apvts.getRawParameterValue (ParamIDs::passThrough);
    genOnParam       = apvts.getRawParameterValue (ParamIDs::genOn);
    genFreqParam     = apvts.getRawParameterValue (ParamIDs::genFreq);
    genLevelParam    = apvts.getRawParameterValue (ParamIDs::genLevel);
    genOutputParam   = apvts.getRawParameterValue (ParamIDs::genOutput);
    genWaveParam     = apvts.getRawParameterValue (ParamIDs::genWave);

    kneeWaveParam     = apvts.getRawParameterValue (ParamIDs::kneeWave);
    kneeFreqParam     = apvts.getRawParameterValue (ParamIDs::kneeFreq);
    kneeDurationParam = apvts.getRawParameterValue (ParamIDs::kneeDuration);
    kneeOutputParam   = apvts.getRawParameterValue (ParamIDs::kneeOutput);

    swModeParam       = apvts.getRawParameterValue (ParamIDs::swMode);
    swLawParam        = apvts.getRawParameterValue (ParamIDs::swLaw);
    swEndParam        = apvts.getRawParameterValue (ParamIDs::swEnd);
    swDurationParam   = apvts.getRawParameterValue (ParamIDs::swDuration);
    swLevelParam      = apvts.getRawParameterValue (ParamIDs::swLevel);
    swOutputParam     = apvts.getRawParameterValue (ParamIDs::swOutput);
    swIntervalParam   = apvts.getRawParameterValue (ParamIDs::swInterval);
    swAveragesParam   = apvts.getRawParameterValue (ParamIDs::swAverages);
    swContinuousParam = apvts.getRawParameterValue (ParamIDs::swContinuous);

    arFreqParam       = apvts.getRawParameterValue (ParamIDs::arFreq);
    arLen1Param       = apvts.getRawParameterValue (ParamIDs::arLen1);
    arLevel1Param     = apvts.getRawParameterValue (ParamIDs::arLevel1);
    arLen2Param       = apvts.getRawParameterValue (ParamIDs::arLen2);
    arPercentParam    = apvts.getRawParameterValue (ParamIDs::arPercent);
    arOutputParam     = apvts.getRawParameterValue (ParamIDs::arOutput);
}

//==============================================================================
namespace
{
    // PolyBLEP: smooths the discontinuities of saw and square so that
    // hardly any aliasing occurs (important for clean FFT measurements)
    inline double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            const double x = t / dt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }

    inline float renderWave (Waveform wave, double t, double dt) noexcept
    {
        switch (wave)
        {
            case Waveform::saw:
                return (float) ((2.0 * t - 1.0) - polyBlep (t, dt));

            case Waveform::triangle:
                // starts at 0 and rises, like the sine
                if (t < 0.25) return (float) (4.0 * t);
                if (t < 0.75) return (float) (2.0 - 4.0 * t);
                return (float) (4.0 * t - 4.0);

            case Waveform::square:
            {
                double v = t < 0.5 ? 1.0 : -1.0;
                v += polyBlep (t, dt);
                v -= polyBlep (std::fmod (t + 0.5, 1.0), dt);
                return (float) v;
            }

            case Waveform::sine:
                break;
        }
        return (float) std::sin (juce::MathConstants<double>::twoPi * t);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const StringArray sources { "Off", "Input L", "Input R", "Generator" };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scopeSourceL, 1 },
                                                        "Scope L Source", sources, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scopeSourceR, 1 },
                                                        "Scope R Source", sources, 2));

    // Time per division in milliseconds (logarithmic distribution)
    {
        NormalisableRange<float> range (0.02f, 100.0f);
        range.setSkewForCentre (2.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float ms, int)
            {
                if (ms < 1.0f)   return String (ms * 1000.0f, 0) + " us/div";
                if (ms < 10.0f)  return String (ms, 2) + " ms/div";
                return String (ms, 1) + " ms/div";
            })
            .withValueFromStringFunction ([] (const String& text)
            {
                const auto value = text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
                return text.containsIgnoreCase ("us") ? value / 1000.0f : value;
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::timePerDiv, 1 },
                                                           "Time/Div", range, 2.0f, attributes));
    }

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::trigger, 1 },
                                                      "Trigger", true));

    // OFF by default to avoid feedback loops in the standalone app
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::passThrough, 1 },
                                                      "Pass Input Through", false));

    // ---------------- Generator ----------------
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::genOn, 1 },
                                                      "Generator On", false));
    {
        NormalisableRange<float> range (20.0f, 20000.0f);
        range.setSkewForCentre (1000.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float hz, int)
            {
                if (hz < 1000.0f) return String (hz, 1) + " Hz";
                return String (hz / 1000.0f, 3) + " kHz";
            })
            .withValueFromStringFunction ([] (const String& text)
            {
                auto value = text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
                if (text.containsIgnoreCase ("k"))
                    value *= 1000.0f;
                return value;
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::genFreq, 1 },
                                                           "Frequency", range, 1000.0f, attributes));
    }
    {
        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float db, int) { return String (db, 1) + " dBFS"; })
            .withValueFromStringFunction ([] (const String& text)
            {
                return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::genLevel, 1 },
                                                           "Amplitude",
                                                           NormalisableRange<float> (-60.0f, 0.0f, 0.1f),
                                                           -12.0f, attributes));
    }
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::genOutput, 1 },
                                                        "Generator Output",
                                                        StringArray { "Off", "L", "R", "L + R" }, 3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::genWave, 1 },
                                                        "Waveform",
                                                        StringArray { "Sine", "Saw", "Triangle", "Square" }, 0));

    // ---------------- FFT view ----------------
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::fftSize, 1 },
                                                        "FFT Size",
                                                        StringArray { "1024", "2048", "4096", "8192", "16384", "32768" }, 3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::fftWindow, 1 },
                                                        "FFT Window",
                                                        StringArray { "Hann", "Blackman-Harris", "Flat-Top", "Rectangular" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::fftAverage, 1 },
                                                        "FFT Averaging",
                                                        StringArray { "Off", "Light", "Medium", "Heavy" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::fftRange, 1 },
                                                        "FFT Range",
                                                        StringArray { "60 dB", "90 dB", "120 dB", "150 dB" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::fftMaxFreq, 1 },
                                                        "FFT Max Frequency",
                                                        StringArray { "20 kHz", "50 kHz", "100 kHz" }, 0));

    // ---------------- Knee test ----------------
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::kneeWave, 1 },
                                                        "Knee Waveform",
                                                        StringArray { "Sine", "Saw", "Triangle", "Square" }, 0));
    {
        NormalisableRange<float> range (20.0f, 20000.0f);
        range.setSkewForCentre (1000.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float hz, int)
            {
                if (hz < 1000.0f) return String (hz, 1) + " Hz";
                return String (hz / 1000.0f, 3) + " kHz";
            })
            .withValueFromStringFunction ([] (const String& text)
            {
                auto value = text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
                if (text.containsIgnoreCase ("k"))
                    value *= 1000.0f;
                return value;
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::kneeFreq, 1 },
                                                           "Knee Frequency", range, 1000.0f, attributes));
    }
    {
        NormalisableRange<float> range (5.0f, 120.0f, 1.0f);
        range.setSkewForCentre (30.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("s")
            .withStringFromValueFunction ([] (float sec, int) { return String ((int) sec) + " s"; })
            .withValueFromStringFunction ([] (const String& text)
            {
                return text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::kneeDuration, 1 },
                                                           "Knee Sweep Duration", range, 20.0f, attributes));
    }
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::kneeOutput, 1 },
                                                        "Knee Output",
                                                        StringArray { "L", "R", "L + R" }, 2));

    // ---------------- Frequency response test ----------------
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swMode, 1 },
                                                        "Sweep Mode", StringArray { "Sine sweep", "Impulse (Dirac)" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swInterval, 1 },
                                                        "Impulse Interval", StringArray { "100 ms", "250 ms", "500 ms", "1 s" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swAverages, 1 },
                                                        "Impulse Averages", StringArray { "1", "4", "16", "64" }, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::swContinuous, 1 },
                                                      "Continuous", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swLaw, 1 },
                                                        "Sweep Law", StringArray { "Linear", "Logarithmic", "LF-weighted (1/f)" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swEnd, 1 },
                                                        "Sweep End", StringArray { "22 kHz", "50 kHz", "100 kHz" }, 0));
    {
        NormalisableRange<float> range (1.0f, 60.0f, 1.0f);
        range.setSkewForCentre (10.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("s")
            .withStringFromValueFunction ([] (float sec, int) { return String ((int) sec) + " s"; })
            .withValueFromStringFunction ([] (const String& text)
            {
                return text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::swDuration, 1 },
                                                           "Sweep Duration", range, 10.0f, attributes));
    }
    {
        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float db, int) { return String (db, 1) + " dBFS"; })
            .withValueFromStringFunction ([] (const String& text)
            {
                return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::swLevel, 1 },
                                                           "Sweep Level", NormalisableRange<float> (-60.0f, 0.0f, 0.1f),
                                                           -12.0f, attributes));
    }
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swOutput, 1 },
                                                        "Sweep Output", StringArray { "L", "R", "L + R" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swGate, 1 },
                                                        "IR Window", StringArray { "5 ms", "20 ms", "100 ms", "500 ms", "Full" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swSmooth, 1 },
                                                        "Smoothing", StringArray { "Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::swRange, 1 },
                                                        "Sweep Display Range", StringArray { "+/- 3 dB", "+/- 12 dB", "+/- 24 dB", "+/- 48 dB" }, 2));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::swNormalize, 1 },
                                                      "Normalize to 1 kHz", true));

    // ---------------- Attack/release test ----------------
    {
        NormalisableRange<float> range (20.0f, 20000.0f);
        range.setSkewForCentre (1000.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float hz, int)
            {
                if (hz < 1000.0f) return String (hz, 1) + " Hz";
                return String (hz / 1000.0f, 3) + " kHz";
            })
            .withValueFromStringFunction ([] (const String& text)
            {
                auto value = text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
                if (text.containsIgnoreCase ("k"))
                    value *= 1000.0f;
                return value;
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arFreq, 1 },
                                                           "A/R Frequency", range, 1000.0f, attributes));
    }
    {
        NormalisableRange<float> range (10.0f, 5000.0f, 1.0f);
        range.setSkewForCentre (300.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float ms, int) { return String ((int) ms) + " ms"; })
            .withValueFromStringFunction ([] (const String& text)
            {
                auto value = text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
                if (text.containsIgnoreCase ("s") && ! text.containsIgnoreCase ("ms"))
                    value *= 1000.0f;
                return value;
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arLen1, 1 },
                                                           "Burst 1 Length", range, 500.0f, attributes));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arLen2, 1 },
                                                           "Burst 2 Length", range, 1000.0f, attributes));
    }
    {
        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float db, int) { return String (db, 1) + " dBFS"; })
            .withValueFromStringFunction ([] (const String& text)
            {
                return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arLevel1, 1 },
                                                           "Burst 1 Level", NormalisableRange<float> (-60.0f, 0.0f, 0.1f),
                                                           -6.0f, attributes));
    }
    {
        // Amplitude of burst 2 in percent of burst 1 (25 % = -12 dB), max. 100 %
        NormalisableRange<float> range (1.0f, 100.0f, 1.0f);
        range.setSkewForCentre (30.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float pct, int)
            {
                return String ((int) pct) + " % (" + String (20.0f * std::log10 (pct / 100.0f), 1) + " dB)";
            })
            .withValueFromStringFunction ([] (const String& text)
            {
                return text.upToFirstOccurrenceOf ("%", false, false)
                           .retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::arPercent, 1 },
                                                           "Burst 2 Level", range, 25.0f, attributes));
    }
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::arOutput, 1 },
                                                        "A/R Output", StringArray { "L", "R", "L + R" }, 2));
    return layout;
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    smoothedFreq.reset (sampleRate, 0.05);
    smoothedFreq.setCurrentAndTargetValue (genFreqParam->load());

    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (0.0f);

    phase = 0.0;

    // A running knee test is aborted when the audio settings change
    kneeFade.reset (sampleRate, 0.01);
    kneeFade.setCurrentAndTargetValue (0.0f);
    kneeActive = false;
    kneeRunning = false;
    kneeWinLength = juce::jmax (1, (int) (sampleRate * 0.02));   // 20 ms per measured point

    // Knee latency compensation: up to 500 ms loop latency
    {
        size_t size = 1;
        while ((double) size < sampleRate * 0.6) size <<= 1;
        kneeSentRing.assign (size, 0.0f);
        kneeLevelRing.assign (size, kneeNoLevel);
        kneeRingMask = size - 1;
    }

    // A running frequency response test is aborted as well
    swFade.reset (sampleRate, 0.005);

    // Ring buffers for the latency-compensated comparison (>= 250 ms, power of two)
    {
        size_t size = 1;
        while ((double) size < sampleRate * 0.25) size <<= 1;
        swSentRing.assign (size, 0.0f);
        swFreqRing.assign (size, -1.0f);
        swRingMask = (int64_t) size - 1;
    }
    swFade.setCurrentAndTargetValue (0.0f);
    swActive = false;
    sweepBusy = false;

    // ... and a running attack/release test
    arActive = false;
    burstBusy = false;
}

//==============================================================================
// Knee test: GUI side
//==============================================================================
void AudioPluginAudioProcessor::startKneeTest() noexcept
{
    stopSweepTest();            // only one test tone at a time
    stopBurstTest();
    ++kneeRunIdShared;          // new run id -> the GUI ignores points from older runs
    kneeStopRequest  = false;
    kneeStartRequest = true;
    kneeRunning      = true;    // shown as running immediately
    kneeProgress     = 0.0f;
}

void AudioPluginAudioProcessor::stopKneeTest() noexcept
{
    kneeStartRequest = false;
    kneeStopRequest  = true;
    kneeRunning      = false;
}

int AudioPluginAudioProcessor::readKneePoints (KneePoint* dest, int maxPoints) noexcept
{
    const auto scope = kneeFifo.read (juce::jmin (maxPoints, kneeFifo.getNumReady()));

    for (int i = 0; i < scope.blockSize1; ++i) dest[i]                   = kneeFifoData[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[scope.blockSize1 + i] = kneeFifoData[(size_t) (scope.startIndex2 + i)];

    return scope.blockSize1 + scope.blockSize2;
}

//==============================================================================
// Knee test: audio side
//==============================================================================
void AudioPluginAudioProcessor::startKneeOnAudioThread (double sr) noexcept
{
    kneeRunId      = kneeRunIdShared.load();
    kneeActive     = true;
    kneeRunning    = true;
    kneePos        = 0;
    kneeLength     = juce::jmax ((int64_t) 1, (int64_t) (kneeDurationParam->load() * sr));

    // First a single click: the strongest returned sample gives the loop latency
    // (interface + device, up to 500 ms).
    kneeStage        = KneeStage::click;
    kneeClickPos     = 0;
    kneeClickLength  = (int64_t) (0.5 * sr);
    kneeClickPeak    = 0.0f;
    kneeClickPeakPos = 0;
    kneeLatency      = 0;
    kneeTailLeft     = 0;
    kneeRingPos      = 0;
    std::fill (kneeSentRing.begin(),  kneeSentRing.end(),  0.0f);
    std::fill (kneeLevelRing.begin(), kneeLevelRing.end(), kneeNoLevel);

    // Lead-in: the sweep starts 500 ms "early", below the start level, and rises
    // continuously. Measuring only begins when the tone reaches kneeStartDb. This covers
    // the fade-in without a jump and without a constant-level phase.
    kneePreRoll    = (int64_t) (0.5 * sr);
    kneePhase      = 0.0;
    kneeSumGen     = 0.0;
    kneeSumRetL    = 0.0;
    kneeSumRetR    = 0.0;
    kneeSumLevelDb = 0.0;
    kneeWinCount   = 0;
    kneeFade.setCurrentAndTargetValue (0.0f);   // the tone fades in after the click
}

void AudioPluginAudioProcessor::pushKneePoint (const KneePoint& point) noexcept
{
    const auto scope = kneeFifo.write (1);   // if the FIFO is full the point is dropped

    if (scope.blockSize1 > 0) kneeFifoData[(size_t) scope.startIndex1] = point;
    else if (scope.blockSize2 > 0) kneeFifoData[(size_t) scope.startIndex2] = point;
}

//==============================================================================
// Frequency response test: GUI side
//==============================================================================
float AudioPluginAudioProcessor::getSweepEndFrequency() const noexcept
{
    static constexpr float choices[] = { 22000.0f, 50000.0f, 100000.0f };
    const float selected = choices[juce::jlimit (0, 2, juce::roundToInt (swEndParam->load()))];
    return juce::jmin (selected, (float) (currentSampleRate.load() * 0.48));   // stay just below Nyquist
}

bool AudioPluginAudioProcessor::startSweepTest()
{
    if (sweepBusy.load())
        return false;   // previous run still finishing on the audio thread

    const double sr = currentSampleRate.load();

    SweepSetup setup;
    setup.mode = (SweepMode) juce::roundToInt (swModeParam->load());
    setup.law  = (SweepLaw)  juce::roundToInt (swLawParam->load());
    setup.f1   = getSweepEndFrequency();
    setup.f0   = setup.law == SweepLaw::linear ? 0.0 : 10.0;   // log and 1/f sweeps cannot start at 0 Hz
    setup.gain = juce::Decibels::decibelsToGain (swLevelParam->load());

    setup.clickLength = (int64_t) (0.2 * sr);    // latency measurement (max. 200 ms loop latency)
    setup.leadIn      = (int64_t) (0.3 * sr);    // settle at the start frequency
    setup.tail        = (int64_t) (0.21 * sr);   // wait for the delayed return + fade-out
    setup.sweepLength = juce::jmax ((int64_t) 1, (int64_t) (swDurationParam->load() * sr));

    // LF-weighted law: below the crossover constant cycles per octave, above it constant
    // time per octave (continuous at the crossover). The number of cycles per octave
    // follows from the total duration:
    //   T = C / ln2 * (1/f0 - 1/fc) + C / fc * log2 (f1 / fc)
    setup.crossover = juce::jmin (1000.0, setup.f1 * 0.5);
    {
        const double duration = (double) setup.sweepLength / sr;
        const double perCycle = (1.0 / 10.0 - 1.0 / setup.crossover) / std::log (2.0)
                              + std::log2 (setup.f1 / setup.crossover) / setup.crossover;
        setup.cyclesPerOctave = duration / perCycle;
    }

    static constexpr double intervals[] = { 0.1, 0.25, 0.5, 1.0 };
    static constexpr int    averages[]  = { 1, 4, 16, 64 };
    setup.interval = (int) (intervals[juce::jlimit (0, 3, juce::roundToInt (swIntervalParam->load()))] * sr);
    setup.averages = averages[juce::jlimit (0, 3, juce::roundToInt (swAveragesParam->load()))];

    // Allocate the impulse response slots here (GUI thread), never on the audio thread
    if (setup.mode == SweepMode::impulse)
    {
        for (int i = 0; i < impulseSlots; ++i)
        {
            impSlotL[i].assign ((size_t) setup.interval, 0.0f);
            impSlotR[i].assign ((size_t) setup.interval, 0.0f);
        }
    }

    impAmplitude  = setup.gain;
    impSampleRate = sr;
    sweepSetup    = setup;

    // Switch off a running knee or attack/release test: only one test signal at a time
    stopKneeTest();
    stopBurstTest();

    ++sweepRunIdShared;
    sweepProgress    = 0.0f;
    sweepStopRequest = false;
    sweepRequested   = true;
    return true;
}

// Speed of the running sweep at frequency f (octaves per second)
double AudioPluginAudioProcessor::octavesPerSecond (double f, double sr) const noexcept
{
    switch (sw.law)
    {
        case SweepLaw::linear:       return swFreqStep * sr / (f * std::log (2.0));
        case SweepLaw::logarithmic:  return std::log2 (sw.f1 / sw.f0) * sr / (double) sw.sweepLength;
        case SweepLaw::inverse:      break;
    }
    return juce::jmin (f, sw.crossover) / sw.cyclesPerOctave;
}

//==============================================================================
// Attack/release test: GUI side
//==============================================================================
bool AudioPluginAudioProcessor::startBurstTest()
{
    if (burstBusy.load())
        return false;   // previous run still finishing on the audio thread

    const double sr = currentSampleRate.load();
    auto& rec = burstRecording;

    rec.sampleRate = sr;
    rec.freq       = juce::jmin (arFreqParam->load(), (float) (sr * 0.45));
    rec.amp1       = juce::Decibels::decibelsToGain (arLevel1Param->load());
    rec.amp2       = rec.amp1 * arPercentParam->load() / 100.0f;
    rec.latency    = 0;

    // Timeline: click (latency), silence, burst 1, burst 2, fade-out, tail
    burstClickLength = (int64_t) (0.2 * sr);
    burstFadeLength  = (int64_t) (0.002 * sr);
    rec.burst1Start  = burstClickLength + (int64_t) (0.3 * sr);
    rec.burst2Start  = rec.burst1Start + (int64_t) (arLen1Param->load() * 0.001 * sr);
    rec.burst2End    = rec.burst2Start + (int64_t) (arLen2Param->load() * 0.001 * sr);

    const auto total = (size_t) (rec.burst2End + burstFadeLength + (int64_t) (0.25 * sr));

    // Allocate here (GUI thread), never on the audio thread
    rec.sent.assign (total, 0.0f);
    rec.returnL.assign (total, 0.0f);
    rec.returnR.assign (total, 0.0f);
    rec.length = 0;

    // Only one test signal at a time
    stopKneeTest();
    stopSweepTest();

    ++burstRunIdShared;
    burstProgress    = 0.0f;
    burstStopRequest = false;
    burstRequested   = true;
    return true;
}

void AudioPluginAudioProcessor::stopBurstTest() noexcept
{
    if (burstRequested.exchange (false))
        return;   // not started yet

    burstStopRequest = true;
}

bool AudioPluginAudioProcessor::readImpulsePeriod (uint64_t index, std::vector<float>& left, std::vector<float>& right) const
{
    // The slot of interval k is overwritten again 8 intervals later
    const auto isValid = [this, index] { return index >= 1 && getImpulsePeriodsDone() - index < impulseSlots - 1; };

    if (! isValid())
        return false;

    const auto slot = (size_t) ((index - 1) % impulseSlots);
    left  = impSlotL[slot];
    right = impSlotR[slot];

    return isValid();   // still valid after copying -> data is consistent
}

void AudioPluginAudioProcessor::stopSweepTest() noexcept
{
    if (sweepRequested.exchange (false))
        return;   // not started yet

    sweepStopRequest = true;
}

int AudioPluginAudioProcessor::readSweepPoints (SweepPoint* dest, int maxPoints) noexcept
{
    const auto scope = sweepFifo.read (juce::jmin (maxPoints, sweepFifo.getNumReady()));

    for (int i = 0; i < scope.blockSize1; ++i) dest[i]                   = sweepFifoData[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[scope.blockSize1 + i] = sweepFifoData[(size_t) (scope.startIndex2 + i)];

    return scope.blockSize1 + scope.blockSize2;
}

void AudioPluginAudioProcessor::pushSweepPoint (const SweepPoint& point) noexcept
{
    const auto scope = sweepFifo.write (1);   // if the FIFO is full the point is dropped

    if (scope.blockSize1 > 0) sweepFifoData[(size_t) scope.startIndex1] = point;
    else if (scope.blockSize2 > 0) sweepFifoData[(size_t) scope.startIndex2] = point;
}

// Main input and output; in the plugin also a stereo sidechain input.
// The standalone app has no sidechain (inputs are chosen in its audio settings).
juce::AudioProcessor::BusesProperties AudioPluginAudioProcessor::makeBusesProperties()
{
    auto props = BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    if (juce::PluginHostType::getPluginLoadedAs() != juce::AudioProcessor::wrapperType_Standalone)
        props = props.withInput ("Sidechain", juce::AudioChannelSet::stereo(), true);

    return props;
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const auto ok = [] (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };

    // Sidechain: mono, stereo or switched off by the host
    for (int bus = 1; bus < layouts.inputBuses.size(); ++bus)
    {
        const auto sc = layouts.getChannelSet (true, bus);
        if (! sc.isDisabled() && ! ok (sc))
            return false;
    }

    return ok (in) && ok (out);
}

//==============================================================================
void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getMainBusNumInputChannels();   // without the sidechain
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const double sr      = currentSampleRate.load();

    // ---------------- Knee test requests from the GUI ----------------
    if (kneeStartRequest.exchange (false))
        startKneeOnAudioThread (sr);

    if (kneeStopRequest.exchange (false))
    {
        kneeActive = false;
        kneeFade.setTargetValue (0.0f);   // short fade-out, no click
    }

    // ---------------- Attack/release test requests ----------------
    if (burstRequested.exchange (false))
    {
        arActive    = true;
        burstBusy   = true;
        arPos       = 0;
        arPhase     = 0.0;
        arClickPeak = 0.0f;
    }

    if (burstStopRequest.exchange (false) && arActive)
    {
        arActive  = false;
        burstRecording.length = 0;   // incomplete: not evaluated
        burstBusy = false;
    }

    // ---------------- Frequency response test requests ----------------
    if (sweepRequested.exchange (false))
    {
        sw        = sweepSetup;
        swRunId   = sweepRunIdShared.load();
        swActive  = true;
        sweepBusy = true;
        swPos     = 0;
        impPos    = 0;
        impPeriod = 0;
        impPeriodsDone.store (0);
        impActiveRunId = swRunId;
        swPhase   = 0.0;
        swFreq    = sw.f0;
        // Per-sample step: linear = Hz, logarithmic = factor, inverse = change of 1/f
        swFreqStep = sw.law == SweepLaw::linear ? (sw.f1 - sw.f0) / (double) sw.sweepLength
                                                : std::pow (sw.f1 / sw.f0, 1.0 / (double) sw.sweepLength);
        swSumGen = swSumL = swSumR = swSumFreq = swWeightSum = 0.0;
        swWinCount = 0;
        swFade.setCurrentAndTargetValue (0.0f);
        swClickPeak = 0.0f;
        swClickPeakPos = 0;
        swRingPos = 0;
        swLatency = 0;
        std::fill (swSentRing.begin(), swSentRing.end(), 0.0f);
        std::fill (swFreqRing.begin(), swFreqRing.end(), -1.0f);

        // LF-weighted law: per-sample change of 1/f below the crossover, factor above it
        if (sw.law == SweepLaw::inverse)
        {
            swFreqStep   = std::log (2.0) / (sw.cyclesPerOctave * sr);
            swHighFactor = std::exp (sw.crossover * std::log (2.0) / (sw.cyclesPerOctave * sr));
        }
    }

    if (sweepStopRequest.exchange (false) && swActive)
    {
        swActive  = false;
        sweepBusy = false;
    }

    // Read parameters once per block
    const bool genOn      = genOnParam->load() > 0.5f;
    const auto genOut     = (GenOutput) juce::roundToInt (genOutputParam->load());
    const auto wave       = (Waveform)  juce::roundToInt (genWaveParam->load());

    const auto kneeWave   = (Waveform)   juce::roundToInt (kneeWaveParam->load());
    const auto kneeOut    = (KneeOutput) juce::roundToInt (kneeOutputParam->load());
    const double kneeDt   = (double) kneeFreqParam->load() / sr;

    // While a knee or sweep test runs the input is never passed through: in a hardware
    // loop (output -> device -> input) that would cause feedback
    // Inputs and outputs are chosen in the audio settings (standalone) or in the DAW.
    // The scope and FFT always show input L and R; the input is not passed through.

    const auto arOut      = (KneeOutput) juce::roundToInt (arOutputParam->load());
    const bool arToR      = arOut != KneeOutput::left;
    const bool arToL      = arOut != KneeOutput::right || numOut == 1;

    const auto swOut      = (KneeOutput) juce::roundToInt (swOutputParam->load());
    const bool swToR      = swOut != KneeOutput::left;
    const bool swToL      = swOut != KneeOutput::right || numOut == 1;
    const bool impContinuous = swContinuousParam->load() > 0.5f;   // may be switched off during a run (both modes)

    smoothedFreq.setTargetValue (genFreqParam->load());
    // The normal generator fades out while a knee test runs, so the test always
    // starts cleanly at the bottom of the sweep
    smoothedGain.setTargetValue (genOn && ! kneeActive && ! swActive && ! arActive
                                     ? juce::Decibels::decibelsToGain (genLevelParam->load()) : 0.0f);

    // Output routing: generator or knee test tone
    const auto routeL = [] (bool toL, bool toR, int outs) { return toL || (outs == 1 && toR); };

    const bool genToL  = routeL (genOut == GenOutput::left || genOut == GenOutput::both,
                                 genOut == GenOutput::right || genOut == GenOutput::both, numOut);
    const bool genToR  = genOut == GenOutput::right || genOut == GenOutput::both;
    const bool kneeToL = routeL (kneeOut != KneeOutput::right, kneeOut != KneeOutput::left, numOut);
    const bool kneeToR = kneeOut != KneeOutput::left;

    // Inputs (with a mono input, R = L)
    const float* inL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    // Plugin: always measure the sidechain, never the main input of the track
    // (silence if the host has switched the sidechain off)
    if (hasSidechain())
    {
        auto sidechain = getBusBuffer (buffer, true, 1);
        inL = sidechain.getNumChannels() > 0 ? sidechain.getReadPointer (0) : nullptr;
        inR = sidechain.getNumChannels() > 1 ? sidechain.getReadPointer (1) : inL;
        sidechainConnected = sidechain.getNumChannels() > 0;
    }

    float* outL = numOut > 0 ? buffer.getWritePointer (0) : nullptr;
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;


    const bool  calTone = calToneOn.load();
    const float calGainL = calToneGain[0].load();
    const float calGainR = calToneGain[1].load();
    double sumSqL = 0.0, sumSqR = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // Read the input first, because input and output share the same buffer
        const float l = inL != nullptr ? inL[i] : 0.0f;
        const float r = inR != nullptr ? inR[i] : 0.0f;

        // Normal generator: phase runs from 0 to 1
        const double dt  = (double) smoothedFreq.getNextValue() / sr;
        const float  gen = smoothedGain.getNextValue() * renderWave (wave, phase, dt);
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;

        // Knee test tone: level rises linearly in dB from kneeStartDb to kneeEndDb
        const float fade  = kneeFade.getNextValue();
        const bool  kneeOn = kneeActive || fade > 0.0f;
        float kneeTone = 0.0f;

        if (kneeOn)
        {
            float levelDb   = kneeNoLevel;   // level of this sample, kneeNoLevel = not measured
            bool  advance   = false;

            if (kneeActive && kneeStage == KneeStage::click)
            {
                // Latency measurement: single click, then wait for the strongest return
                kneeTone = kneeClickPos == 0 ? kneeClickGain : 0.0f;

                const float v = std::abs (l) + std::abs (r);
                if (v > kneeClickPeak) { kneeClickPeak = v; kneeClickPeakPos = kneeClickPos; }

                if (++kneeClickPos >= kneeClickLength)
                {
                    kneeLatency = kneeClickPeak > 1.0e-4f * kneeClickGain ? (int) kneeClickPeakPos : 0;   // no return -> 0
                    kneeStage   = KneeStage::sweep;
                    kneeFade.setTargetValue (1.0f);
                }
            }
            else
            {
                if (kneeActive && kneeStage == KneeStage::sweep)
                {
                    // Same slope during the lead-in (kneePreRoll > 0) and the sweep itself
                    const double position = (double) kneePos - (double) kneePreRoll;
                    const float  db = kneeStartDb + (kneeEndDb - kneeStartDb) * (float) (position / (double) kneeLength);
                    kneeLevelGain = juce::Decibels::decibelsToGain (db, -200.0f);

                    if (kneePreRoll > 0)
                        --kneePreRoll;   // lead-in: tone is still below the start level, no measurement
                    else
                    {
                        levelDb = db;
                        advance = true;
                    }
                }

                // (in the tail the tone stays at the end level until the delayed return is measured)
                kneeTone = fade * kneeLevelGain * renderWave (kneeWave, kneePhase, kneeDt);
                kneePhase += kneeDt;
                if (kneePhase >= 1.0)
                    kneePhase -= 1.0;
            }

            // Remember what was sent; the return is compared with the tone sent one loop
            // latency earlier. Without this the rising level would make every returned
            // point too low by (slope x latency), e.g. 1 dB at 12 dB/s and 80 ms.
            const auto ringIndex = (size_t) kneeRingPos & kneeRingMask;
            kneeSentRing[ringIndex]  = kneeTone;
            kneeLevelRing[ringIndex] = levelDb;

            if (kneeActive && kneeRingPos >= (int64_t) kneeLatency)
            {
                const auto delayed  = (size_t) (kneeRingPos - kneeLatency) & kneeRingMask;
                const float sentDb  = kneeLevelRing[delayed];

                if (sentDb > kneeNoLevel + 1.0f)
                {
                    // Measure sent and returned energy (inputs L and R at the same time)
                    // over 20 ms windows. The output level is given relative to the input
                    // level (same detector for both), so a device in bypass gives a perfect
                    // diagonal for every waveform.
                    const float sent = kneeSentRing[delayed];
                    kneeSumGen     += (double) sent * sent;
                    kneeSumRetL    += (double) l * l;
                    kneeSumRetR    += (double) r * r;
                    kneeSumLevelDb += sentDb;

                    if (++kneeWinCount >= kneeWinLength)
                    {
                        if (kneeSumGen > 1.0e-30)
                        {
                            const float inDb  = (float) (kneeSumLevelDb / kneeWinCount);
                            const float gainL = (float) (10.0 * std::log10 (juce::jmax (kneeSumRetL, 1.0e-30) / kneeSumGen));
                            const float gainR = (float) (10.0 * std::log10 (juce::jmax (kneeSumRetR, 1.0e-30) / kneeSumGen));
                            pushKneePoint ({ inDb, inDb + gainL, inDb + gainR, kneeRunId });
                        }

                        kneeSumGen = kneeSumRetL = kneeSumRetR = kneeSumLevelDb = 0.0;
                        kneeWinCount = 0;
                    }
                }
            }
            ++kneeRingPos;

            if (advance && ++kneePos >= kneeLength)
            {
                // Sweep finished: hold the end level until the last returned window is measured
                kneeStage    = KneeStage::tail;
                kneeTailLeft = (int64_t) kneeLatency + kneeWinLength + 1;
            }
            else if (kneeActive && kneeStage == KneeStage::tail && --kneeTailLeft <= 0)
            {
                kneeActive  = false;
                kneeRunning = false;
                kneeFade.setTargetValue (0.0f);   // fade out the tone
            }
        }

        // ---------------- Frequency response sweep ----------------
        float sweepTone = 0.0f;

        if (swActive && sw.mode == SweepMode::impulse)
        {
            // Dirac impulse: one single sample at the start of every interval
            sweepTone = impPos == 0 ? sw.gain : 0.0f;

            // The first interval is a warm-up and is not used. From the second one on,
            // the returned signal of each interval is one (periodic) impulse response.
            if (impPeriod >= 1)
            {
                const auto slot = (size_t) ((impPeriod - 1) % impulseSlots);
                impSlotL[slot][(size_t) impPos] = l;
                impSlotR[slot][(size_t) impPos] = r;
            }

            if (++impPos >= sw.interval)
            {
                impPos = 0;

                if (impPeriod >= 1)
                    impPeriodsDone.store (impPeriod, std::memory_order_release);

                ++impPeriod;

                if (! impContinuous && impPeriod > (uint64_t) sw.averages)
                {
                    swActive  = false;
                    sweepBusy = false;
                }
            }
        }
        else if (swActive)
        {
            // Timeline of one sweep run (positions in samples):
            //   [0, startLeadIn)        click for the latency measurement (first sweep only)
            //   [startLeadIn, startSw)  lead-in: tone at the start frequency (settling)
            //   [startSw, endSw)        the sweep itself
            //   [endSw, total)          tail: wait for the delayed return, fade-out
            const int64_t startLeadIn = sw.clickLength;
            const int64_t startSw     = startLeadIn + sw.leadIn;
            const int64_t endSw       = startSw + sw.sweepLength;
            const int64_t total       = endSw + sw.tail;

            const bool inClick = swPos < startLeadIn;
            const bool inSweep = swPos >= startSw && swPos < endSw;

            if (swPos == startLeadIn)
                swFade.setTargetValue (1.0f);            // fade in the lead-in tone
            if (swPos == endSw && ! impContinuous)
                swFade.setTargetValue (0.0f);            // short fade-out at the end frequency

            if (inClick)
            {
                // Single-sample click; the strongest returned sample gives the loop latency
                sweepTone = swPos == 0 ? sw.gain : 0.0f;

                const float v = std::abs (l) + std::abs (r);
                if (v > swClickPeak) { swClickPeak = v; swClickPeakPos = swPos; }

                if (swPos == startLeadIn - 1)
                    swLatency = swClickPeak > 1.0e-4f * sw.gain ? (int) swClickPeakPos : 0;   // no return -> 0
            }
            else
            {
                sweepTone = swFade.getNextValue() * sw.gain * (float) std::sin (juce::MathConstants<double>::twoPi * swPhase);
                swPhase += swFreq / sr;
                if (swPhase >= 1.0)
                    swPhase -= 1.0;
            }

            // Remember what was sent (tone and frequency) so the return can be compared
            // with the signal that was sent one loop latency earlier
            // (indexed with a continuous counter: swPos jumps back in continuous mode)
            const auto ringIndex = (size_t) (swRingPos & swRingMask);
            swSentRing[ringIndex] = sweepTone;
            swFreqRing[ringIndex] = inSweep ? (float) swFreq : -1.0f;   // -1 = not part of the sweep

            // Level tracking: ratio of returned to sent energy per window, both weighted
            // with a Hann window. Over whole periods the Hann weighting cancels the
            // ripple of partial cycles, so even windows of only 2..4 periods are accurate.
            if (! inClick && swRingPos >= swLatency)
            {
                const auto delayed = (size_t) ((swRingPos - swLatency) & swRingMask);
                const float  sent  = swSentRing[delayed];
                const double freq  = swFreqRing[delayed];

                if (freq > 0.0)
                {
                    if (swWinCount == 0)
                    {
                        // 4 periods, but no longer than the time the sweep needs for 1/3 octave;
                        // at least 2 periods (otherwise the level cannot be measured)
                        const double f        = juce::jmax (1.0, freq);
                        const double period   = sr / f;
                        const double maxSmear = (1.0 / 3.0) / juce::jmax (1.0e-6, octavesPerSecond (f, sr)) * sr;
                        const double length   = juce::jmax (2.0 * period, juce::jmin (4.0 * period, maxSmear));
                        swWinLength = (int) juce::jlimit (16.0, sr, length);
                    }

                    const double phaseInWindow = ((double) swWinCount + 0.5) / (double) swWinLength;
                    const double w = std::pow (std::sin (juce::MathConstants<double>::pi * phaseInWindow), 2.0);

                    swSumGen    += w * sent * sent;
                    swSumL      += w * l * l;
                    swSumR      += w * r * r;
                    swSumFreq   += w * freq;
                    swWeightSum += w;

                    if (++swWinCount >= swWinLength)
                    {
                        const double fAvg = swSumFreq / juce::jmax (1.0e-12, swWeightSum);

                        if (swSumGen > 1.0e-30 && fAvg >= 5.0)
                            pushSweepPoint ({ (float) fAvg,
                                              (float) (10.0 * std::log10 (juce::jmax (swSumL, 1.0e-30) / swSumGen)),
                                              (float) (10.0 * std::log10 (juce::jmax (swSumR, 1.0e-30) / swSumGen)),
                                              swRunId });

                        swSumGen = swSumL = swSumR = swSumFreq = swWeightSum = 0.0;
                        swWinCount = 0;
                    }
                }
                else if (swWinCount > 0)
                {
                    // Sweep boundary inside a window: drop the incomplete window
                    swSumGen = swSumL = swSumR = swSumFreq = swWeightSum = 0.0;
                    swWinCount = 0;
                }
            }

            // Advance the frequency
            if (inSweep)
            {
                if (sw.law == SweepLaw::linear)
                    swFreq += swFreqStep;
                else if (sw.law == SweepLaw::logarithmic)
                    swFreq *= swFreqStep;
                else if (swFreq < sw.crossover)
                    swFreq = 1.0 / (1.0 / swFreq - swFreqStep);    // constant cycles per octave
                else
                    swFreq *= swHighFactor;                         // constant time per octave

                swFreq = juce::jmin (swFreq, sw.f1);
            }

            // Continuous: start the next sweep right away with a new lead-in
            // (no new click: the latency is already known)
            if (swPos == endSw - 1 && impContinuous)
            {
                swPos  = startLeadIn - 1;   // becomes startLeadIn below
                swFreq = sw.f0;
            }

            ++swRingPos;

            if (++swPos >= total)
            {
                swActive  = false;
                sweepBusy = false;
            }
        }

        // ---------------- Attack/release bursts ----------------
        float burstTone = 0.0f;

        if (arActive)
        {
            auto& rec = burstRecording;

            if (arPos < burstClickLength)
            {
                // Click for the latency measurement: the strongest returned sample
                // after the click gives the loop latency
                burstTone = arPos == 0 ? rec.amp1 : 0.0f;

                const float v = std::abs (l) + std::abs (r);
                if (v > arClickPeak && v > 1.0e-4f * rec.amp1)
                {
                    arClickPeak = v;
                    rec.latency = (int) arPos;
                }
            }
            else if (arPos >= rec.burst1Start && arPos < rec.burst2End + burstFadeLength)
            {
                // Burst 1, then burst 2 directly afterwards (continuous phase),
                // a 2 ms fade-out after the end of burst 2
                float amp = arPos < rec.burst2Start ? rec.amp1 : rec.amp2;
                if (arPos >= rec.burst2End)
                    amp *= 1.0f - (float) (arPos - rec.burst2End) / (float) burstFadeLength;

                burstTone = amp * (float) std::sin (juce::MathConstants<double>::twoPi * arPhase);
                arPhase += (double) rec.freq / sr;
                if (arPhase >= 1.0)
                    arPhase -= 1.0;
            }

            rec.sent[(size_t) arPos]    = burstTone;
            rec.returnL[(size_t) arPos] = l;
            rec.returnR[(size_t) arPos] = r;

            if (++arPos >= (int64_t) rec.sent.size())
            {
                arActive   = false;
                rec.length = arPos;
                burstBusy  = false;    // GUI may now evaluate the recording
            }
        }

        // Scope and FFT show the inputs
        scopeBuffer.push (l, r);

        float oL = 0.0f;
        float oR = 0.0f;

        // Knee tone (zero when no test runs) + normal generator (fades to zero during a test)
        if (kneeOn)
        {
            if (kneeToL) oL += kneeTone;
            if (kneeToR) oR += kneeTone;
        }

        if (swToL) oL += sweepTone;
        if (swToR) oR += sweepTone;

        if (arToL) oL += burstTone;
        if (arToR) oR += burstTone;

        if (genToL) oL += gen;
        if (genToR) oR += gen;

        // Calibration reference tone replaces everything else
        if (calTone)
        {
            const float tone = (float) std::sin (juce::MathConstants<double>::twoPi * calTonePhase);
            calTonePhase += 1000.0 / sr;
            if (calTonePhase >= 1.0)
                calTonePhase -= 1.0;
            oL = calGainL * tone;
            oR = calGainR * tone;
        }

        sumSqL += (double) l * (double) l;
        sumSqR += (double) r * (double) r;

        if (outL != nullptr) outL[i] = oL;
        if (outR != nullptr) outR[i] = oR;
    }

    // Input meter for the calibration (time constant about 0.3 s)
    if (numSamples > 0)
    {
        const float a = 1.0f - (float) std::exp (-(double) numSamples / (0.3 * sr));
        for (int ch = 0; ch < 2; ++ch)
        {
            const float blockMs = (float) ((ch == 0 ? sumSqL : sumSqR) / (double) numSamples);
            const float prev    = inputMeanSquare[(size_t) ch].load (std::memory_order_relaxed);
            inputMeanSquare[(size_t) ch].store (prev + a * (blockMs - prev), std::memory_order_relaxed);
        }
    }

    if (kneeActive)
        kneeProgress = (float) ((double) kneePos / (double) kneeLength);
    else if (! kneeRunning.load())
        kneeProgress = 0.0f;

    if (arActive)
        burstProgress = (float) ((double) arPos / (double) juce::jmax ((size_t) 1, burstRecording.sent.size()));

    if (swActive)
        sweepProgress = sw.mode == SweepMode::impulse
                            ? (impContinuous ? 0.0f : (float) juce::jlimit (0.0, 1.0, (double) impPeriod / (double) (sw.averages + 1)))
                            : (float) juce::jlimit (0.0, 1.0, (double) swPos / (double) (sw.clickLength + sw.leadIn + sw.sweepLength + sw.tail));
    // (in continuous mode the progress shows the position within the current sweep)

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            readCalibrationFromState();
        }
}

//==============================================================================
// Units and calibration
//==============================================================================
namespace
{
    const juce::Identifier unitProp ("unitDbu");
    const juce::Identifier calInProps[]  { "calInL",  "calInR" };
    const juce::Identifier calOutProps[] { "calOutL", "calOutR" };
}

void AudioPluginAudioProcessor::readCalibrationFromState()
{
    const auto& state = apvts.state;
    unitDbu = (bool) state.getProperty (unitProp, false);

    for (int ch = 0; ch < 2; ++ch)
    {
        calIn[(size_t) ch]  = (float) (double) state.getProperty (calInProps[ch],  18.0);
        calOut[(size_t) ch] = (float) (double) state.getProperty (calOutProps[ch], 18.0);
    }
}

void AudioPluginAudioProcessor::setUnitDbu (bool dbu)
{
    unitDbu = dbu;
    apvts.state.setProperty (unitProp, dbu, nullptr);
}

void AudioPluginAudioProcessor::setInputCalibration (int channel, float dbuAtFullScale)
{
    channel = juce::jlimit (0, 1, channel);
    calIn[(size_t) channel] = dbuAtFullScale;
    apvts.state.setProperty (calInProps[channel], dbuAtFullScale, nullptr);
}

void AudioPluginAudioProcessor::setOutputCalibration (int channel, float dbuAtFullScale)
{
    channel = juce::jlimit (0, 1, channel);
    calOut[(size_t) channel] = dbuAtFullScale;
    apvts.state.setProperty (calOutProps[channel], dbuAtFullScale, nullptr);
}

float AudioPluginAudioProcessor::getInputLevelDb (int channel) const noexcept
{
    const float ms = inputMeanSquare[(size_t) juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    // RMS of a sine * sqrt(2) = its peak -> mean square * 2
    return juce::Decibels::gainToDecibels (std::sqrt (2.0f * ms), -120.0f);
}

void AudioPluginAudioProcessor::setCalibrationTone (bool on, float levelDbL, float levelDbR) noexcept
{
    calToneGain[0] = juce::Decibels::decibelsToGain (juce::jmin (0.0f, levelDbL));
    calToneGain[1] = juce::Decibels::decibelsToGain (juce::jmin (0.0f, levelDbR));
    calToneOn      = on;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
