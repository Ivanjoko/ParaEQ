#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "EqEngine.h"

inline juce::String bandId (int band, const juce::String& name)
{
    return "b" + juce::String (band + 1) + "_" + name;
}

class ParaEQProcessor : public juce::AudioProcessor
{
public:
    ParaEQProcessor();
    ~ParaEQProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ParaEQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Datos para la interfaz
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<double> currentSr { 44100.0 };

    static constexpr int fftOrder  = 12;
    static constexpr int fftSize   = 1 << fftOrder;
    static constexpr int scopeSize = 1 << 13;
    std::array<std::atomic<float>, scopeSize> scope;   // anillo con la salida (mono)
    std::atomic<int> scopePos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    EqParams readParams() const;
    template <typename T> void process (juce::AudioBuffer<T>&);

    EqEngine engine;

    std::atomic<float> *pIn = nullptr, *pOut = nullptr, *pBypass = nullptr,
                       *pHpfOn = nullptr, *pHpfFreq = nullptr, *pHpfSlope = nullptr,
                       *pLpfOn = nullptr, *pLpfFreq = nullptr, *pLpfSlope = nullptr;

    struct BandPtrs { std::atomic<float> *on, *type, *freq, *gain, *q; };
    std::array<BandPtrs, EqMath::numBands> pBand {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQProcessor)
};
