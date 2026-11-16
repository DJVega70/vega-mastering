#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "DSPModules.h"

struct MeterData
{
    std::atomic<float> inL { 0 }, inR { 0 }, outL { 0 }, outR { 0 };   // linear peaks since last read
    std::atomic<float> truePeakRecent { 0 };                          // linear, since last read
    std::atomic<float> truePeakMax { -100.0f };                        // dBTP hold
    std::atomic<float> compGr { 0 }, limGr { 0 };                      // dB, positive, since last read
    std::atomic<float> correlation { 1.0f };
    std::atomic<float> integrated { -100.0f };

    static void atomicMax (std::atomic<float>& a, float v)
    {
        float prev = a.load (std::memory_order_relaxed);
        while (prev < v && ! a.compare_exchange_weak (prev, v, std::memory_order_relaxed)) {}
    }
};

class VegaMasteringProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    VegaMasteringProcessor();
    ~VegaMasteringProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vega Productions Mastering"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // ---- Presets
    static int getNumFactoryPresets();
    static juce::String getFactoryPresetName (int index);
    void loadFactoryPreset (int index);
    bool saveUserPreset (const juce::File& file);
    bool loadUserPreset (const juce::File& file);
    static juce::File getUserPresetFolder();
    juce::String currentPresetName { "Init" };
    int currentPresetIndex = 0;

    // ---- A/B
    void selectABSlot (int slot);
    void copyToOtherSlot();
    int getABSlot() const { return abSlot; }

    // ---- Metering / UI
    void resetLoudness();
    juce::AudioProcessorValueTreeState apvts;
    MeterData meters;
    vega::LoudnessMeter loudness;
    vega::StereoFifo analyserFifo { 32768 };
    vega::StereoFifo scopeFifo { 16384 };
    std::atomic<float> uiScale { 1.0f };
    std::atomic<int> eqUiChannel { 0 };   // 0 = Mid, 1 = Side (UI state only)
    double getCurrentSampleRate() const { return sampleRateHz; }

private:
    void timerCallback() override;
    void processChunk (float* L, float* R, int n);
    int computeLatency (int osIndex, bool linear) const;
    vega::EqSettings readEq (char ch) const;
    void rebuildLinearPhaseIr();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    std::array<std::array<std::atomic<float>*, 7>, 2> eqPtrs {};
    double sampleRateHz = 44100.0;
    int maxChunk = 512, lookaheadBase = 66, firOrder = 12, lastOsIndex = -1;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> oversamplers;
    std::array<vega::NonLinearStage, 4> nonLinear;
    vega::TruePeakLimiter limiter;
    vega::TruePeakInterpolator truePeakMeter;

    vega::ThreeBandEq eqMid, eqSide;
    vega::Biquad monoBassHpf;
    float lastMonoBass = -1.0f;
    juce::dsp::Convolution linearEq;
    vega::GlueCompressor compressor;

    std::vector<float> dryRing[2];
    int dryPos = 0, dryMask = 0;
    juce::AudioBuffer<float> dryBuf;

    juce::SmoothedValue<float> inGainS, outGainS, limGainS, bypassS;
    double corrLR = 0, corrLL = 0, corrRR = 0, corrCoef = 0.999;

    // linear‑phase IR bookkeeping (message thread)
    vega::EqSettings irMid, irSide;
    bool irEqOn = true, irValid = false;
    int ticksSinceIr = 100, ticks = 0;

    // A/B
    juce::ValueTree abStates[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaMasteringProcessor)
};
