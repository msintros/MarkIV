// Instantiates the plugin processor without a GUI and pushes audio through
// every channel, EQ mode, cab mode and oversampling setting.
#include <juce_audio_processors/juce_audio_processors.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    proc->setPlayConfigDetails (2, 2, 48000.0, 256);
    proc->prepareToPlay (48000.0, 256);

    auto set = [&] (const char* id, float plain)
    {
        for (auto* p : proc->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->getParameterID() == id)
                    rp->setValueNotifyingHost (rp->convertTo0to1 (plain));
    };

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    double phase = 0;
    bool ok = true;
    for (int ch = 0; ch < 3; ++ch)
        for (int eq = 0; eq < 3; ++eq)
            for (int cab = 0; cab < 3; ++cab)
            {
                set ("channel", (float) ch);
                set ("eqMode", (float) eq);
                set ("cabMode", (float) cab);
                set ("eq750", -9.0f);
                float peak = 0;
                for (int block = 0; block < 40; ++block)
                {
                    for (int i = 0; i < 256; ++i)
                    {
                        const float x = 0.3f * (float) std::sin (phase);
                        phase += 2 * juce::MathConstants<double>::pi * 196.0 / 48000.0;
                        buffer.setSample (0, i, x);
                        buffer.setSample (1, i, x);
                    }
                    proc->processBlock (buffer, midi);
                    peak = juce::jmax (peak, buffer.getMagnitude (0, 0, 256));
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 256; ++i)
                            ok = ok && std::isfinite (buffer.getSample (c, i));
                }
                std::printf ("channel %d eq %d cab %d: peak %.3f\n", ch, eq, cab, peak);
            }

    // Oversampling change goes through prepareToPlay.
    for (int q = 0; q < 3; ++q)
    {
        set ("quality", (float) q);
        proc->prepareToPlay (48000.0, 256);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const int blocks = 48000 / 256 * 2;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 256; ++i)
                buffer.setSample (0, i, 0.3f * (float) std::sin (phase += 0.025));
            proc->processBlock (buffer, midi);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("oversampling %dx, lead channel: %.1f%% of one core\n", 2 << q, ms / 2000.0 * 100.0);
    }

    juce::MemoryBlock state;
    proc->getStateInformation (state);
    proc->setStateInformation (state.getData(), (int) state.getSize());
    std::printf ("state %d bytes, latency %d samples\n", (int) state.getSize(), proc->getLatencySamples());
    std::printf (ok ? "plugin smoke test passed\n" : "NON-FINITE OUTPUT\n");
    return ok ? 0 : 1;
}
