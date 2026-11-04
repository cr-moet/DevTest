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
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const StringArray sources { "Aus", "Eingang L", "Eingang R", "Generator" };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scopeSourceL, 1 },
                                                        "Oszi L Quelle", sources, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scopeSourceR, 1 },
                                                        "Oszi R Quelle", sources, 2));

    // Zeit pro Division in Millisekunden (logarithmisch verteilt)
    {
        NormalisableRange<float> range (0.02f, 100.0f);
        range.setSkewForCentre (2.0f);

        auto attributes = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float ms, int)
            {
                if (ms < 1.0f)   return String (ms * 1000.0f, 0) + " us/Div";
                if (ms < 10.0f)  return String (ms, 2) + " ms/Div";
                return String (ms, 1) + " ms/Div";
            })
            .withValueFromStringFunction ([] (const String& text)
            {
                const auto value = text.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
                return text.containsIgnoreCase ("us") ? value / 1000.0f : value;
            });

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::timePerDiv, 1 },
                                                           "Zeit/Div", range, 2.0f, attributes));
    }

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::trigger, 1 },
                                                      "Trigger", true));

    // Standardmaessig AUS, damit im Standalone keine Rueckkopplung entsteht
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::passThrough, 1 },
                                                      "Eingang durchleiten", false));

    // ---------------- Generator ----------------
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::genOn, 1 },
                                                      "Generator an", false));
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
                                                           "Frequenz", range, 1000.0f, attributes));
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
                                                        "Generator Ausgang",
                                                        StringArray { "Aus", "L", "R", "L + R" }, 3));
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

    // Parameter einmal pro Block lesen
    const auto srcL       = (ScopeSource) juce::roundToInt (scopeSrcLParam->load());
    const auto srcR       = (ScopeSource) juce::roundToInt (scopeSrcRParam->load());
    const bool passThru   = passThroughParam->load() > 0.5f;
    const bool genOn      = genOnParam->load() > 0.5f;
    const auto genOut     = (GenOutput) juce::roundToInt (genOutputParam->load());

    smoothedFreq.setTargetValue (genFreqParam->load());
    smoothedGain.setTargetValue (genOn ? juce::Decibels::decibelsToGain (genLevelParam->load()) : 0.0f);

    bool genToL = genOut == GenOutput::left  || genOut == GenOutput::both;
    const bool genToR = genOut == GenOutput::right || genOut == GenOutput::both;

    // Mono-Ausgang: Generator landet auf dem einzigen Kanal, egal ob L oder R gewaehlt
    if (numOut == 1)
        genToL = genToL || genToR;

    // Eingaenge (bei Mono-Eingang wird R = L)
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

    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    for (int i = 0; i < numSamples; ++i)
    {
        // Eingang zuerst lesen, da Ein- und Ausgang denselben Puffer teilen
        const float l = inL != nullptr ? inL[i] : 0.0f;
        const float r = inR != nullptr ? inR[i] : 0.0f;

        // Sinus erzeugen
        const float gen = smoothedGain.getNextValue() * (float) std::sin (phase);
        phase += twoPi * (double) smoothedFreq.getNextValue() / sr;
        if (phase >= twoPi)
            phase -= twoPi;

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
