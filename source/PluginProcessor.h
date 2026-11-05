#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ScopeBuffer.h"

//==============================================================================
// Parameter IDs in one place, so processor and editor use the same ones
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
    inline constexpr const char* genWave      = "genWave";
    inline constexpr const char* fftSize      = "fftSize";
    inline constexpr const char* fftWindow    = "fftWindow";
    inline constexpr const char* fftAverage   = "fftAverage";
    inline constexpr const char* fftRange     = "fftRange";
    inline constexpr const char* fftMaxFreq   = "fftMaxFreq";
    inline constexpr const char* kneeWave     = "kneeWave";
    inline constexpr const char* kneeFreq     = "kneeFreq";
    inline constexpr const char* kneeDuration = "kneeDuration";
    inline constexpr const char* kneeOutput   = "kneeOutput";
}

// Order must match the choice parameters
enum class ScopeSource { off = 0, inputL, inputR, generator };
enum class GenOutput   { off = 0, left, right, both };
enum class Waveform    { sine = 0, saw, triangle, square };
enum class KneeOutput  { left = 0, right, both };

// One measured point of the knee test (levels in dBFS)
struct KneePoint
{
    float inputDb   = 0.0f;   // level sent to the device under test
    float outputDbL = 0.0f;   // level coming back on input L
    float outputDbR = 0.0f;   // level coming back on input R
    uint32_t runId = 0;      // which test run this point belongs to
};

// Fixed sweep range of the knee test
inline constexpr float kneeStartDb = -60.0f;
inline constexpr float kneeEndDb   = 0.0f;

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

    //==========================================================================
    // Knee test (compressor transfer curve). Called from the GUI thread.
    void startKneeTest() noexcept;
    void stopKneeTest() noexcept;
    bool isKneeTestRunning() const noexcept  { return kneeRunning.load(); }
    float getKneeProgress() const noexcept   { return kneeProgress.load(); }
    uint32_t getKneeRunId() const noexcept   { return kneeRunIdShared.load(); }

    // Reads new measurement points (GUI thread). Returns the number of points read.
    int readKneePoints (KneePoint* dest, int maxPoints) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Pointers to the parameter values (thread-safe to read)
    std::atomic<float>* scopeSrcLParam  = nullptr;
    std::atomic<float>* scopeSrcRParam  = nullptr;
    std::atomic<float>* passThroughParam = nullptr;
    std::atomic<float>* genOnParam      = nullptr;
    std::atomic<float>* genFreqParam    = nullptr;
    std::atomic<float>* genLevelParam   = nullptr;
    std::atomic<float>* genOutputParam  = nullptr;
    std::atomic<float>* genWaveParam    = nullptr;

    // ---------------- Knee test ----------------
    void startKneeOnAudioThread (double sr) noexcept;
    void pushKneePoint (const KneePoint& point) noexcept;

    std::atomic<float>* kneeWaveParam     = nullptr;
    std::atomic<float>* kneeFreqParam     = nullptr;
    std::atomic<float>* kneeDurationParam = nullptr;
    std::atomic<float>* kneeOutputParam   = nullptr;

    // GUI -> audio thread requests
    std::atomic<bool>     kneeStartRequest { false }, kneeStopRequest { false };
    std::atomic<bool>     kneeRunning { false };
    std::atomic<float>    kneeProgress { 0.0f };
    std::atomic<uint32_t> kneeRunIdShared { 0 };

    // Audio thread -> GUI: measured points (lock-free FIFO)
    static constexpr int kneeFifoSize = 16384;
    juce::AbstractFifo     kneeFifo { kneeFifoSize };
    std::vector<KneePoint> kneeFifoData = std::vector<KneePoint> ((size_t) kneeFifoSize);

    // Audio thread state
    bool     kneeActive = false;
    uint32_t kneeRunId = 0;
    int64_t  kneePos = 0, kneeLength = 1;
    int64_t  kneePreRoll = 0;   // remaining settling samples at the start level (no measurement)
    double   kneePhase = 0.0;
    float    kneeLevelGain = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> kneeFade { 0.0f };
    double   kneeSumGen = 0.0, kneeSumRetL = 0.0, kneeSumRetR = 0.0, kneeSumLevelDb = 0.0;
    int      kneeWinCount = 0, kneeWinLength = 960;

    // Signal generator (phase 0..1)
    double phase = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedFreq { 1000.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedGain { 0.0f };

    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
