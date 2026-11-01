#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class MarkIVEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MarkIVEditor (MarkIVProcessor&);
    ~MarkIVEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
    };
    struct Group
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    Knob& addKnob (const char* paramId, const juce::String& text);
    Knob& addEqSlider (const char* paramId, const juce::String& text);
    Toggle& addToggle (const char* paramId, const juce::String& text);
    Choice& addChoice (const char* paramId, const juce::String& text);
    void timerCallback() override;
    void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);

    MarkIVProcessor& processor;
    juce::LookAndFeel_V4 lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<Group> groups;

    std::array<juce::TextButton, 3> channelButtons;
    juce::TextButton loadIrButton { "Load IR..." };
    juce::Label irLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastChannel = -1;

    // Named handles for layout.
    Knob *r1Gain{}, *r1Treble{}, *r1Master{}, *r1Presence{}, *bass{}, *mid{};
    Knob *r2Gain{}, *r2Treble{}, *r2Master{}, *r2Presence{};
    Knob *ldGain{}, *ldTreble{}, *ldBass{}, *ldMid{}, *ldDrive{}, *ldMaster{}, *ldPresence{};
    Knob *outLevel{}, *inTrim{}, *outTrim{};
    std::array<Knob*, 5> eqSliders {};
    Toggle *r1Bright{}, *r2Fat{}, *ldFat{}, *ldBright{};
    Choice *voicing{}, *eqMode{}, *cabMode{}, *quality{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MarkIVEditor)
};
