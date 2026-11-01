// Speaker cabinet stage. The schematic stops before the power amp, so a
// preamp output on its own sounds harsh; this provides either a simple
// built-in 4x12-style voicing or a user impulse response.
#pragma once

#include <juce_dsp/juce_dsp.h>

class CabSim
{
public:
    enum class Mode { Off = 0, BuiltIn = 1, ImpulseResponse = 2 };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setMode (Mode m) { mode = m; }
    void loadImpulseResponse (const juce::File& file);
    bool hasImpulseResponse() const { return irLoaded; }
    void process (float* data, int numSamples);

private:
    Mode mode = Mode::BuiltIn;
    double fs = 48000.0;
    bool irLoaded = false;

    using Filter = juce::dsp::IIR::Filter<float>;
    Filter highPass, lowResonance, boxDip, upperMid, lowPass1, lowPass2;
    juce::dsp::Convolution convolution;
};
