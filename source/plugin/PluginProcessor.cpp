#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

namespace
{
    // Preamp output is in volts (several volts at the Output Level wiper).
    // Scale so that a typical crunch setting lands near -12 dBFS.
    constexpr double kVoltsToFull = 0.1;
    // 0 dBFS at the input is treated as 1 V peak from the guitar.
    constexpr double kFullToVolts = 1.0;
    // Knob smoothing and how often the circuit matrices may be rebuilt.
    constexpr int kControlBlock = 32;
    constexpr double kSmoothingSeconds = 0.03;

    const char* knobIds[] = {
        params::r1Gain, params::r1Treble, params::r1Master, params::bass, params::mid,
        params::r2Gain, params::r2Treble, params::r2Master,
        params::leadGain, params::leadTreble, params::leadBass, params::leadMid, params::leadDrive, params::leadMaster,
        params::outputLevel
    };
}

MarkIVProcessor::MarkIVProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MarkIV", params::createLayout())
{
    for (int k = 0; k < numKnobs; ++k)
        knobParams[(size_t) k] = apvts.getRawParameterValue (knobIds[k]);
    apvts.addParameterListener (params::quality, this);
}

MarkIVProcessor::~MarkIVProcessor()
{
    apvts.removeParameterListener (params::quality, this);
    cancelPendingUpdate();
}

bool MarkIVProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void MarkIVProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    configure (sampleRate, samplesPerBlock);
}

void MarkIVProcessor::configure (double sampleRate, int samplesPerBlock)
{
    baseRate = sampleRate;
    maxBlock = juce::jmax (samplesPerBlock, 1);
    osFactorLog2 = 1 + (int) apvts.getRawParameterValue (params::quality)->load();

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        1, (size_t) osFactorLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing ((size_t) maxBlock);
    setLatencySamples ((int) std::round (oversampling->getLatencyInSamples()));

    readParameters (true);
    preamp.setParams (smoothedParams (0));
    preamp.prepare (baseRate * (1 << osFactorLog2));

    eq.prepare (baseRate);
    eq.setGains ({ eqCurrent[0], eqCurrent[1], eqCurrent[2], eqCurrent[3], eqCurrent[4] });

    juce::dsp::ProcessSpec spec { baseRate, (juce::uint32) maxBlock, 1 };
    presence.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (baseRate, 3000.0, 0.7, 1.0f);
    presence.prepare (spec);
    presenceDb = -100.0f;
    cab.prepare (baseRate, maxBlock);
    {
        const juce::ScopedLock sl (irLock);
        if (irFile.existsAsFile())
            cab.loadImpulseResponse (irFile);
    }
}

void MarkIVProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == params::quality)
        triggerAsyncUpdate();
}

void MarkIVProcessor::handleAsyncUpdate()
{
    // Changing the oversampling factor rebuilds the circuit at the new rate.
    if (getSampleRate() <= 0)
        return;
    suspendProcessing (true);
    {
        const juce::ScopedLock sl (getCallbackLock());
        configure (getSampleRate(), getBlockSize());
    }
    suspendProcessing (false);
}

void MarkIVProcessor::readParameters (bool snap)
{
    for (int k = 0; k < numKnobs; ++k)
    {
        knobTarget[(size_t) k] = knobParams[(size_t) k]->load() / 10.0;
        if (snap)
            knobCurrent[(size_t) k] = knobTarget[(size_t) k];
    }
    for (int b = 0; b < 5; ++b)
    {
        eqTarget[(size_t) b] = apvts.getRawParameterValue (params::eqBands[b])->load();
        if (snap)
            eqCurrent[(size_t) b] = eqTarget[(size_t) b];
    }
}

mk4::PreampParams MarkIVProcessor::smoothedParams (int numSamples)
{
    if (numSamples > 0)
    {
        const double a = 1.0 - std::exp (-numSamples / (kSmoothingSeconds * baseRate));
        for (int k = 0; k < numKnobs; ++k)
        {
            auto& c = knobCurrent[(size_t) k];
            const double t = knobTarget[(size_t) k];
            c = std::abs (t - c) < 1e-4 ? t : c + (t - c) * a;
        }
    }

    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const auto& c = knobCurrent;
    mk4::PreampParams p;
    p.channel = (mk4::Channel) (int) raw (params::channel);
    p.r1Gain = c[kR1Gain]; p.r1Treble = c[kR1Treble]; p.r1Master = c[kR1Master];
    p.r1Bright = raw (params::r1Bright) > 0.5f;
    p.bass = c[kBass]; p.mid = c[kMid];
    p.r2Gain = c[kR2Gain]; p.r2Treble = c[kR2Treble]; p.r2Master = c[kR2Master];
    p.r2Fat = raw (params::r2Fat) > 0.5f;
    p.leadGain = c[kLeadGain]; p.leadTreble = c[kLeadTreble]; p.leadBass = c[kLeadBass]; p.leadMid = c[kLeadMid];
    p.leadDrive = c[kLeadDrive]; p.leadMaster = c[kLeadMaster];
    p.leadFat = raw (params::leadFat) > 0.5f;
    p.leadBright = raw (params::leadBright) > 0.5f;
    p.midGain = (int) raw (params::voicing) == 1;
    p.outputLevel = c[kOutputLevel];
    return p;
}

void MarkIVProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || oversampling == nullptr)
        return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    readParameters (false);

    // Guitar is mono: use the first input channel.
    float* mono = buffer.getWritePointer (0);
    const float inGain = (float) (kFullToVolts * juce::Decibels::decibelsToGain (apvts.getRawParameterValue (params::inputTrim)->load()));
    juce::FloatVectorOperations::multiply (mono, inGain, numSamples);

    // ---- Preamp at the oversampled rate.
    float* channels[] = { mono };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
    auto up = oversampling->processSamplesUp (block);
    float* upData = up.getChannelPointer (0);
    const int factor = 1 << osFactorLog2;
    const int upSamples = (int) up.getNumSamples();

    for (int start = 0; start < upSamples; start += kControlBlock * factor)
    {
        const int len = juce::jmin (kControlBlock * factor, upSamples - start);
        preamp.setParams (smoothedParams (len / factor));
        preamp.advance (len);
        for (int i = start; i < start + len; ++i)
            upData[i] = (float) (preamp.processSample (upData[i]) * kVoltsToFull);
    }
    oversampling->processSamplesDown (block);

    // ---- Graphic EQ (linear, so it runs at the base rate), crossfaded in/out.
    const int channelIdx = (int) apvts.getRawParameterValue (params::channel)->load();
    const int eqModeIdx = (int) apvts.getRawParameterValue (params::eqMode)->load();
    const bool eqOn = eqModeIdx == 1 || (eqModeIdx == 2 && channelIdx == 1);
    const double mixStep = 1.0 / (0.02 * baseRate);
    for (int start = 0; start < numSamples; start += kControlBlock)
    {
        const int len = juce::jmin (kControlBlock, numSamples - start);
        const double a = 1.0 - std::exp (-len / (kSmoothingSeconds * baseRate));
        bool changed = false;
        for (size_t b = 0; b < 5; ++b)
        {
            const double t = eqTarget[b];
            if (std::abs (eqCurrent[b] - t) > 0.0)
            {
                eqCurrent[b] = std::abs (t - eqCurrent[b]) < 0.01 ? t : eqCurrent[b] + (t - eqCurrent[b]) * a;
                changed = true;
            }
        }
        if (changed)
            eq.setGains ({ eqCurrent[0], eqCurrent[1], eqCurrent[2], eqCurrent[3], eqCurrent[4] });

        for (int i = start; i < start + len; ++i)
        {
            const double dry = mono[i];
            const double wet = eq.processSample (dry);
            eqMix = eqOn ? juce::jmin (1.0, eqMix + mixStep) : juce::jmax (0.0, eqMix - mixStep);
            mono[i] = (float) (dry + eqMix * (wet - dry));
        }
    }

    // ---- Presence (approximation of the power amp's presence control).
    const char* presenceIds[] = { params::r1Presence, params::r2Presence, params::leadPresence };
    const float pres = apvts.getRawParameterValue (presenceIds[juce::jlimit (0, 2, channelIdx)])->load();
    const float targetDb = (pres - 3.0f) * 1.2f;   // knob 0..10 -> -3.6 .. +8.4 dB shelf
    if (std::abs (targetDb - presenceDb) > 0.05f)
    {
        presenceDb = presenceDb < -50.0f ? targetDb : presenceDb + (targetDb - presenceDb) * 0.3f;
        *presence.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            baseRate, 3000.0f, 0.7f, juce::Decibels::decibelsToGain (presenceDb));
    }
    for (int i = 0; i < numSamples; ++i)
        mono[i] = presence.processSample (mono[i]);

    // ---- Cabinet.
    cab.setMode ((CabSim::Mode) (int) apvts.getRawParameterValue (params::cabMode)->load());
    {
        const juce::ScopedTryLock sl (irLock);
        if (sl.isLocked())
            cab.process (mono, numSamples);
    }

    const float outGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (params::outputTrim)->load());
    juce::FloatVectorOperations::multiply (mono, outGain, numSamples);

    for (int ch = 1; ch < getTotalNumOutputChannels(); ++ch)
        buffer.copyFrom (ch, 0, mono, numSamples);
}

void MarkIVProcessor::loadImpulseResponse (const juce::File& file)
{
    const juce::ScopedLock sl (irLock);
    irFile = file;
    cab.loadImpulseResponse (file);
}

juce::AudioProcessorEditor* MarkIVProcessor::createEditor()
{
    return new MarkIVEditor (*this);
}

void MarkIVProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("irFile", irFile.getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MarkIVProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.isValid())
        {
            const juce::String ir = state.getProperty ("irFile").toString();
            apvts.replaceState (state);
            if (ir.isNotEmpty())
                loadImpulseResponse (juce::File (ir));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MarkIVProcessor();
}
