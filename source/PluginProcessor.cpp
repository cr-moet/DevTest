#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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
}

//==============================================================================
// Knee test: GUI side
//==============================================================================
void AudioPluginAudioProcessor::startKneeTest() noexcept
{
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

    // Lead-in: the sweep starts 500 ms "early", below the start level, and rises
    // continuously. Measuring only begins when the tone reaches kneeStartDb. This covers
    // the fade-in and the latency of the loop (interface + device) without a jump and
    // without a constant-level phase.
    kneePreRoll    = (int64_t) (0.5 * sr);
    kneePhase      = 0.0;
    kneeSumGen     = 0.0;
    kneeSumRetL    = 0.0;
    kneeSumRetR    = 0.0;
    kneeSumLevelDb = 0.0;
    kneeWinCount   = 0;
    kneeFade.setTargetValue (1.0f);
}

void AudioPluginAudioProcessor::pushKneePoint (const KneePoint& point) noexcept
{
    const auto scope = kneeFifo.write (1);   // if the FIFO is full the point is dropped

    if (scope.blockSize1 > 0) kneeFifoData[(size_t) scope.startIndex1] = point;
    else if (scope.blockSize2 > 0) kneeFifoData[(size_t) scope.startIndex2] = point;
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const auto ok = [] (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };

    return ok (in) && ok (out);
}

//==============================================================================
void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
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

    // Read parameters once per block
    const auto srcL       = (ScopeSource) juce::roundToInt (scopeSrcLParam->load());
    const auto srcR       = (ScopeSource) juce::roundToInt (scopeSrcRParam->load());
    const bool genOn      = genOnParam->load() > 0.5f;
    const auto genOut     = (GenOutput) juce::roundToInt (genOutputParam->load());
    const auto wave       = (Waveform)  juce::roundToInt (genWaveParam->load());

    const auto kneeWave   = (Waveform)   juce::roundToInt (kneeWaveParam->load());
    const auto kneeOut    = (KneeOutput) juce::roundToInt (kneeOutputParam->load());
    const double kneeDt   = (double) kneeFreqParam->load() / sr;

    // While a knee test runs the input is never passed through: in a hardware
    // loop (output -> device -> input) that would cause feedback
    const bool passThru   = passThroughParam->load() > 0.5f && ! kneeActive;

    smoothedFreq.setTargetValue (genFreqParam->load());
    // The normal generator fades out while a knee test runs, so the test always
    // starts cleanly at the bottom of the sweep
    smoothedGain.setTargetValue (genOn && ! kneeActive ? juce::Decibels::decibelsToGain (genLevelParam->load()) : 0.0f);

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

    float* outL = numOut > 0 ? buffer.getWritePointer (0) : nullptr;
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    const auto pick = [] (ScopeSource s, float l, float r, float g)
    {
        switch (s)
        {
            case ScopeSource::inputL:    return l;
            case ScopeSource::inputR:    return r;
            case ScopeSource::generator: return g;
            case ScopeSource::off:       break;
        }
        return 0.0f;
    };

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
            float levelDb = 0.0f;

            if (kneeActive)
            {
                // Same slope during the lead-in (kneePreRoll > 0) and the sweep itself
                const double position = (double) kneePos - (double) kneePreRoll;
                levelDb = kneeStartDb + (kneeEndDb - kneeStartDb) * (float) (position / (double) kneeLength);
                kneeLevelGain = juce::Decibels::decibelsToGain (levelDb, -200.0f);
            }

            kneeTone = fade * kneeLevelGain * renderWave (kneeWave, kneePhase, kneeDt);
            kneePhase += kneeDt;
            if (kneePhase >= 1.0)
                kneePhase -= 1.0;

            if (kneeActive && kneePreRoll > 0)
            {
                --kneePreRoll;   // lead-in: tone is still below the start level, no measurement
            }
            else if (kneeActive)
            {
                // Measure sent and returned energy (inputs L and R at the same time)
                // over 20 ms windows. The output level is given relative to the input
                // level (same detector for both), so a device in bypass gives a perfect
                // diagonal for every waveform.
                kneeSumGen     += (double) kneeTone * kneeTone;
                kneeSumRetL    += (double) l * l;
                kneeSumRetR    += (double) r * r;
                kneeSumLevelDb += levelDb;

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

                if (++kneePos >= kneeLength)
                {
                    // Sweep finished: fade out the tone
                    kneeActive  = false;
                    kneeRunning = false;
                    kneeFade.setTargetValue (0.0f);
                }
            }
        }

        // The scope/FFT source "Generator" shows the knee tone while the test runs
        const float genForScope = kneeOn ? kneeTone : gen;
        scopeBuffer.push (pick (srcL, l, r, genForScope), pick (srcR, l, r, genForScope));

        float oL = passThru ? l : 0.0f;
        float oR = passThru ? r : 0.0f;

        // Knee tone (zero when no test runs) + normal generator (fades to zero during a test)
        if (kneeOn)
        {
            if (kneeToL) oL += kneeTone;
            if (kneeToR) oR += kneeTone;
        }

        if (genToL) oL += gen;
        if (genToR) oR += gen;

        if (outL != nullptr) outL[i] = oL;
        if (outR != nullptr) outR[i] = oR;
    }

    if (kneeActive)
        kneeProgress = (float) ((double) kneePos / (double) kneeLength);
    else if (! kneeRunning.load())
        kneeProgress = 0.0f;

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
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
