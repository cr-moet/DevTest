#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ScopeBuffer.h"

//==============================================================================
// Parameter-IDs an einer Stelle, damit Processor und Editor dieselben nutzen
//==============================================================================
namespace ParamIDs
{
    inline constexpr const char* scopeSourceL = "scopeSrcL";
    inline constexpr const char* scopeSourceR = "scopeSrcR";
    inline constexpr const char* timePerDiv   = "timePerDiv";
    inline constexpr const char* trigger      = "trigger";
    inline constexpr const char* passThrough  = "passThrough";
    inline constexpr const char* genOn        = "genOn";
    inline constexpr const char* genFreq      = "genFreq";
    inline constexpr const char* genLevel     = "genLevel";
    inline constexpr const char* genOutput    = "genOutput";
}

// Reihenfolge muss zu den Choice-Parametern passen
enum class ScopeSource { off = 0, inputL, inputR, generator };
enum class GenOutput   { off = 0, left, right, both };

//==============================================================================
class AudioPluginAudioProcessor final : public juce::AudioProcessor
{
public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return JucePlugin_Name; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    ScopeBuffer scopeBuffer;

    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Zeiger auf die Parameterwerte (thread-sicher lesbar)
    std::atomic<float>* scopeSrcLParam  = nullptr;
    std::atomic<float>* scopeSrcRParam  = nullptr;
    std::atomic<float>* passThroughParam = nullptr;
    std::atomic<float>* genOnParam      = nullptr;
    std::atomic<float>* genFreqParam    = nullptr;
    std::atomic<float>* genLevelParam   = nullptr;
    std::atomic<float>* genOutputParam  = nullptr;

    // Sinusgenerator
    double phase = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedFreq { 1000.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedGain { 0.0f };

    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
