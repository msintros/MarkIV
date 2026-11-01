#pragma once

#include "CabSim.h"
#include "GraphicEQ.h"
#include "MarkIVPreamp.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class MarkIVProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    MarkIVProcessor();
    ~MarkIVProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void loadImpulseResponse (const juce::File& file);
    juce::String getImpulseResponseName() const { return irFile.getFileNameWithoutExtension(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void configure (double sampleRate, int samplesPerBlock);
    void readParameters (bool snap);
    mk4::PreampParams smoothedParams (int numSamples);

    // Continuous knobs that are smoothed before they reach the circuit.
    enum Knob { kR1Gain, kR1Treble, kR1Master, kBass, kMid, kR2Gain, kR2Treble, kR2Master,
                kLeadGain, kLeadTreble, kLeadBass, kLeadMid, kLeadDrive, kLeadMaster, kOutputLevel, numKnobs };
    std::array<std::atomic<float>*, numKnobs> knobParams {};
    std::array<double, numKnobs> knobCurrent {}, knobTarget {};
    std::array<double, 5> eqCurrent {}, eqTarget {};

    mk4::MarkIVPreamp preamp;
    mk4::GraphicEQ eq;
    CabSim cab;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::IIR::Filter<float> presence;
    float presenceDb = -100.0f;
    double eqMix = 0.0;

    double baseRate = 48000.0;
    int maxBlock = 512;
    int osFactorLog2 = 2;

    juce::File irFile;
    juce::CriticalSection irLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MarkIVProcessor)
};
