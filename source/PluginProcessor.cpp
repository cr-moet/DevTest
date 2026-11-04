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

    // Read parameters once per block
    const auto srcL       = (ScopeSource) juce::roundToInt (scopeSrcLParam->load());
    const auto srcR       = (ScopeSource) juce::roundToInt (scopeSrcRParam->load());
    const bool passThru   = passThroughParam->load() > 0.5f;
    const bool genOn      = genOnParam->load() > 0.5f;
    const auto genOut     = (GenOutput) juce::roundToInt (genOutputParam->load());
    const auto wave       = (Waveform)  juce::roundToInt (genWaveParam->load());

    smoothedFreq.setTargetValue (genFreqParam->load());
    smoothedGain.setTargetValue (genOn ? juce::Decibels::decibelsToGain (genLevelParam->load()) : 0.0f);

    bool genToL = genOut == GenOutput::left  || genOut == GenOutput::both;
    const bool genToR = genOut == GenOutput::right || genOut == GenOutput::both;

    // Mono output: the generator goes to the only channel, whether L or R is selected
    if (numOut == 1)
        genToL = genToL || genToR;

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

        // Generator: phase runs from 0 to 1
        const double dt  = (double) smoothedFreq.getNextValue() / sr;
        const float  gen = smoothedGain.getNextValue() * renderWave (wave, phase, dt);
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;

        scopeBuffer.push (pick (srcL, l, r, gen), pick (srcR, l, r, gen));

        if (outL != nullptr) outL[i] = (passThru ? l : 0.0f) + (genToL ? gen : 0.0f);
        if (outR != nullptr) outR[i] = (passThru ? r : 0.0f) + (genToR ? gen : 0.0f);
    }

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
