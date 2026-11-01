#include "CabSim.h"

void CabSim::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    using C = juce::dsp::IIR::Coefficients<float>;
    const auto sr = fs;
    highPass.coefficients     = C::makeHighPass (sr, 75.0, 0.8);
    lowResonance.coefficients = C::makePeakFilter (sr, 110.0, 1.2, juce::Decibels::decibelsToGain (4.0f));
    boxDip.coefficients       = C::makePeakFilter (sr, 450.0, 1.0, juce::Decibels::decibelsToGain (-4.0f));
    upperMid.coefficients     = C::makePeakFilter (sr, 2400.0, 1.5, juce::Decibels::decibelsToGain (4.0f));
    lowPass1.coefficients     = C::makeLowPass (sr, 4800.0, 0.9);
    lowPass2.coefficients     = C::makeLowPass (sr, 6500.0, 0.6);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
    for (auto* f : { &highPass, &lowResonance, &boxDip, &upperMid, &lowPass1, &lowPass2 })
        f->prepare (spec);
    convolution.prepare (spec);
    reset();
}

void CabSim::reset()
{
    for (auto* f : { &highPass, &lowResonance, &boxDip, &upperMid, &lowPass1, &lowPass2 })
        f->reset();
    convolution.reset();
}

void CabSim::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return;
    convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes, 0,
                                     juce::dsp::Convolution::Normalise::yes);
    irLoaded = true;
}

void CabSim::process (float* data, int numSamples)
{
    if (mode == Mode::Off)
        return;

    if (mode == Mode::ImpulseResponse && irLoaded)
    {
        float* channels[] = { data };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float x = data[i];
        x = highPass.processSample (x);
        x = lowResonance.processSample (x);
        x = boxDip.processSample (x);
        x = upperMid.processSample (x);
        x = lowPass1.processSample (x);
        x = lowPass2.processSample (x);
        data[i] = x;
    }
}
