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
    inline constexpr const char* swMode       = "swMode";
    inline constexpr const char* swLaw        = "swLaw";
    inline constexpr const char* swEnd        = "swEnd";
    inline constexpr const char* swDuration   = "swDuration";
    inline constexpr const char* swLevel      = "swLevel";
    inline constexpr const char* swOutput     = "swOutput";
    inline constexpr const char* swGate       = "swGate";
    inline constexpr const char* swSmooth     = "swSmooth";
    inline constexpr const char* swRange      = "swRange";
    inline constexpr const char* swNormalize  = "swNormalize";
    inline constexpr const char* swInterval   = "swInterval";
    inline constexpr const char* swAverages   = "swAverages";
    inline constexpr const char* swContinuous = "swContinuous";
    inline constexpr const char* arFreq       = "arFreq";
    inline constexpr const char* arLen1       = "arLen1";
    inline constexpr const char* arLevel1     = "arLevel1";
    inline constexpr const char* arLen2       = "arLen2";
    inline constexpr const char* arPercent    = "arPercent";
    inline constexpr const char* arOutput     = "arOutput";
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

// Frequency response test
enum class SweepMode { level = 0, impulse };      // sine sweep (level tracking) / Dirac impulses
// linear:      constant Hz per second
// logarithmic: constant time per octave
// inverse:     "LF-weighted (1/f)": constant number of cycles per octave below
//              1 kHz (enough time at low frequencies even for short sweeps),
//              logarithmic above it
enum class SweepLaw  { linear = 0, logarithmic, inverse };

// One measured point of the level-tracking sweep (gain relative to the sent level)
struct SweepPoint
{
    float freq  = 0.0f;
    float gainL = 0.0f;   // dB
    float gainR = 0.0f;   // dB
    uint32_t runId = 0;
};

// Attack/release test: recording of two consecutive sine bursts (owned by the processor)
struct BurstRecording
{
    std::vector<float> sent, returnL, returnR;
    int64_t length = 0;                       // valid samples (0 = no complete recording)
    double  sampleRate = 48000.0;
    float   freq = 1000.0f;
    float   amp1 = 0.5f, amp2 = 0.125f;       // linear amplitudes of burst 1 and burst 2
    int64_t burst1Start = 0, burst2Start = 0, burst2End = 0;
    int     latency = 0;                      // loop latency in samples (from the click)
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

    //==========================================================================
    // Frequency response test. Called from the GUI thread.
    // In impulse mode, startSweepTest() allocates the buffers for the returned
    // impulse responses. Returns false if a previous run is still finishing.
    bool startSweepTest();
    void stopSweepTest() noexcept;
    bool isSweepRunning() const noexcept     { return sweepRequested.load() || sweepBusy.load(); }
    float getSweepProgress() const noexcept  { return sweepProgress.load(); }
    uint32_t getSweepRunId() const noexcept  { return sweepRunIdShared.load(); }
    int readSweepPoints (SweepPoint* dest, int maxPoints) noexcept;

    // Impulse mode: one Dirac impulse per interval. The returned signal of each
    // interval (= one impulse response) is handed to the GUI through 8 slots.
    uint64_t getImpulsePeriodsDone() const noexcept    { return impPeriodsDone.load (std::memory_order_acquire); }
    uint32_t getImpulseActiveRunId() const noexcept    { return impActiveRunId.load(); }
    float    getImpulseAmplitude() const noexcept      { return impAmplitude.load(); }
    double   getImpulseSampleRate() const noexcept     { return impSampleRate.load(); }

    // Copies the impulse response of a finished interval (1-based index).
    // Returns false if that slot has already been overwritten.
    bool readImpulsePeriod (uint64_t index, std::vector<float>& left, std::vector<float>& right) const;

    // End frequency of the sweep: selected value, limited to just below Nyquist
    float getSweepEndFrequency() const noexcept;

    //==========================================================================
    // Attack/release test. Called from the GUI thread.
    bool startBurstTest();
    void stopBurstTest() noexcept;
    bool isBurstRunning() const noexcept     { return burstRequested.load() || burstBusy.load(); }
    float getBurstProgress() const noexcept  { return burstProgress.load(); }
    uint32_t getBurstRunId() const noexcept  { return burstRunIdShared.load(); }

    // Valid only while no burst test is running
    const BurstRecording& getBurstRecording() const noexcept { return burstRecording; }

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

    // ---------------- Frequency response test ----------------
    std::atomic<float>* swModeParam     = nullptr;
    std::atomic<float>* swLawParam      = nullptr;
    std::atomic<float>* swEndParam      = nullptr;
    std::atomic<float>* swDurationParam = nullptr;
    std::atomic<float>* swLevelParam    = nullptr;
    std::atomic<float>* swOutputParam   = nullptr;
    std::atomic<float>* swIntervalParam = nullptr;
    std::atomic<float>* swAveragesParam = nullptr;
    std::atomic<float>* swContinuousParam = nullptr;

    void pushSweepPoint (const SweepPoint& point) noexcept;

    // GUI -> audio thread
    std::atomic<bool>     sweepRequested { false }, sweepStopRequest { false };
    std::atomic<bool>     sweepBusy { false };          // audio thread is generating / recording
    std::atomic<float>    sweepProgress { 0.0f };
    std::atomic<uint32_t> sweepRunIdShared { 0 };

    // Settings prepared by the GUI for the next run (read by the audio thread at start)
    struct SweepSetup
    {
        SweepMode mode = SweepMode::level;
        SweepLaw  law  = SweepLaw::logarithmic;
        double f0 = 10.0, f1 = 20000.0;
        int64_t clickLength = 0, leadIn = 0, sweepLength = 1, tail = 0;
        double crossover = 1000.0, cyclesPerOctave = 10.0;   // LF-weighted law
        float gain = 0.25f;
        int interval = 12000;    // impulse mode: samples per impulse
        int averages = 4;        // impulse mode: number of impulses (single run)
    } sweepSetup;

    // Audio thread -> GUI: measured points
    static constexpr int sweepFifoSize = 32768;
    juce::AbstractFifo      sweepFifo { sweepFifoSize };
    std::vector<SweepPoint> sweepFifoData = std::vector<SweepPoint> ((size_t) sweepFifoSize);

    // Impulse mode: slots for the returned impulse responses
    static constexpr int impulseSlots = 8;
    std::vector<float> impSlotL[impulseSlots], impSlotR[impulseSlots];
    std::atomic<uint64_t> impPeriodsDone { 0 };
    std::atomic<uint32_t> impActiveRunId { 0 };
    std::atomic<float>    impAmplitude { 1.0f };
    std::atomic<double>   impSampleRate { 48000.0 };
    int      impPos = 0;
    uint64_t impPeriod = 0;

    // Audio thread state
    bool     swActive = false;
    uint32_t swRunId = 0;
    SweepSetup sw;
    int64_t  swPos = 0;
    double   swPhase = 0.0, swFreq = 10.0, swFreqStep = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> swFade { 0.0f };
    double   swSumGen = 0.0, swSumL = 0.0, swSumR = 0.0, swSumFreq = 0.0;
    int      swWinCount = 0, swWinLength = 960;
    double   swWeightSum = 0.0;
    double   swHighFactor = 1.0;

    // Latency compensation: sent tone and frequency of the last ~250 ms
    std::vector<float> swSentRing, swFreqRing;
    int64_t  swRingMask = 0, swRingPos = 0;
    float    swClickPeak = 0.0f;
    int64_t  swClickPeakPos = 0;
    int      swLatency = 0;

    double octavesPerSecond (double f, double sr) const noexcept;

    // ---------------- Attack/release test ----------------
    std::atomic<float>* arFreqParam    = nullptr;
    std::atomic<float>* arLen1Param    = nullptr;
    std::atomic<float>* arLevel1Param  = nullptr;
    std::atomic<float>* arLen2Param    = nullptr;
    std::atomic<float>* arPercentParam = nullptr;
    std::atomic<float>* arOutputParam  = nullptr;

    std::atomic<bool>     burstRequested { false }, burstStopRequest { false }, burstBusy { false };
    std::atomic<float>    burstProgress { 0.0f };
    std::atomic<uint32_t> burstRunIdShared { 0 };

    BurstRecording burstRecording;
    int64_t burstClickLength = 0, burstFadeLength = 0;

    // Audio thread state
    bool    arActive = false;
    int64_t arPos = 0;
    double  arPhase = 0.0;
    float   arClickPeak = 0.0f;

    // Signal generator (phase 0..1)
    double phase = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedFreq { 1000.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         smoothedGain { 0.0f };

    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
