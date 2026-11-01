#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace params
{
// Channel and switches
inline constexpr const char* channel    = "channel";
inline constexpr const char* r1Bright   = "r1Bright";
inline constexpr const char* r2Fat      = "r2Fat";
inline constexpr const char* leadFat    = "leadFat";
inline constexpr const char* leadBright = "leadBright";
inline constexpr const char* voicing    = "voicing";     // Harmonics / Mid Gain

// Knobs (0..10 like the panel)
inline constexpr const char* r1Gain       = "r1Gain";
inline constexpr const char* r1Treble     = "r1Treble";
inline constexpr const char* r1Master     = "r1Master";
inline constexpr const char* r1Presence   = "r1Presence";
inline constexpr const char* bass         = "bass";
inline constexpr const char* mid          = "mid";
inline constexpr const char* r2Gain       = "r2Gain";
inline constexpr const char* r2Treble     = "r2Treble";
inline constexpr const char* r2Master     = "r2Master";
inline constexpr const char* r2Presence   = "r2Presence";
inline constexpr const char* leadGain     = "leadGain";
inline constexpr const char* leadTreble   = "leadTreble";
inline constexpr const char* leadBass     = "leadBass";
inline constexpr const char* leadMid      = "leadMid";
inline constexpr const char* leadDrive    = "leadDrive";
inline constexpr const char* leadMaster   = "leadMaster";
inline constexpr const char* leadPresence = "leadPresence";
inline constexpr const char* outputLevel  = "outputLevel";

// Graphic EQ
inline constexpr const char* eqMode = "eqMode";      // Out / In / Auto R2
inline constexpr const char* eqBands[5] = { "eq80", "eq240", "eq750", "eq2200", "eq6600" };

// Plugin extras
inline constexpr const char* inputTrim  = "inputTrim";
inline constexpr const char* outputTrim = "outputTrim";
inline constexpr const char* cabMode    = "cabMode";     // Off / Built-in / Impulse response
inline constexpr const char* quality    = "quality";     // oversampling 2x / 4x / 8x

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    int version = 1;

    auto knob = [&] (const char* id, const String& name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name,
                                                           NormalisableRange<float> (0.0f, 10.0f, 0.01f), def));
    };
    auto toggle = [&] (const char* id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, version }, name, def));
    };
    auto choice = [&] (const char* id, const String& name, StringArray items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, items, def));
    };

    choice (channel, "Channel", { "Rhythm 1", "Rhythm 2", "Lead" }, 0);

    knob (r1Gain, "R1 Gain", 6.0f);
    toggle (r1Bright, "R1 Pull Bright", false);
    knob (r1Treble, "R1 Treble", 6.5f);
    knob (bass, "R1+R2 Bass", 2.5f);
    knob (mid, "R1+R2 Mid", 6.0f);
    knob (r1Master, "R1 Master", 5.0f);
    knob (r1Presence, "R1 Presence", 5.0f);

    knob (r2Gain, "R2 Gain", 8.5f);
    toggle (r2Fat, "R2 Pull Fat", false);
    knob (r2Treble, "R2 Treble", 6.5f);
    knob (r2Master, "R2 Master", 5.0f);
    knob (r2Presence, "R2 Presence", 5.0f);

    knob (leadGain, "Lead Gain", 7.5f);
    toggle (leadFat, "Lead Pull Fat", false);
    knob (leadTreble, "Lead Treble", 6.5f);
    knob (leadBass, "Lead Bass", 2.0f);
    knob (leadMid, "Lead Mid", 6.0f);
    knob (leadDrive, "Lead Drive", 6.0f);
    toggle (leadBright, "Lead Bright", false);
    knob (leadMaster, "Lead Master", 5.0f);
    knob (leadPresence, "Lead Presence", 5.0f);
    choice (voicing, "Lead Voicing", { "Harmonics", "Mid Gain" }, 0);

    knob (outputLevel, "Output Level", 5.0f);

    choice (eqMode, "EQ", { "Out", "In", "Auto R2" }, 0);
    const char* bandNames[5] = { "EQ 80 Hz", "EQ 240 Hz", "EQ 750 Hz", "EQ 2200 Hz", "EQ 6600 Hz" };
    for (int b = 0; b < 5; ++b)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { eqBands[b], version }, bandNames[b],
                                                           NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { inputTrim, version }, "Input Trim",
                                                       NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { outputTrim, version }, "Output Trim",
                                                       NormalisableRange<float> (-36.0f, 12.0f, 0.1f), -6.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    choice (cabMode, "Cabinet", { "Off", "Built-in", "Impulse response" }, 1);
    choice (quality, "Oversampling", { "2x", "4x", "8x" }, 1);
    return layout;
}
} // namespace params
