#include "PluginEditor.h"

#include "Parameters.h"

namespace
{
    const juce::Colour kBackground { 0xff17181a };
    const juce::Colour kPanel { 0xff222326 };
    const juce::Colour kAccent { 0xffd9b45a };     // brass
    const juce::Colour kText { 0xffe8e3d6 };
    const juce::Colour kLeadRed { 0xffc8453b };

    constexpr int kWidth = 1040, kHeight = 600;
}

MarkIVEditor::MarkIVEditor (MarkIVProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3b40));
    lnf.setColour (juce::Slider::thumbColourId, kText);
    lnf.setColour (juce::Slider::trackColourId, kAccent);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::Slider::textBoxTextColourId, kText.withAlpha (0.8f));
    lnf.setColour (juce::Label::textColourId, kText);
    lnf.setColour (juce::ToggleButton::textColourId, kText);
    lnf.setColour (juce::ToggleButton::tickColourId, kAccent);
    lnf.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2d2e32));
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff45464c));
    lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2d2e32));
    lnf.setColour (juce::TextButton::buttonOnColourId, kAccent);
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setLookAndFeel (&lnf);

    using namespace params;
    r1Gain = &addKnob (params::r1Gain, "Gain");
    r1Treble = &addKnob (params::r1Treble, "Treble");
    r1Master = &addKnob (params::r1Master, "Master");
    r1Presence = &addKnob (params::r1Presence, "Presence");
    bass = &addKnob (params::bass, "Bass");
    mid = &addKnob (params::mid, "Mid");
    r2Gain = &addKnob (params::r2Gain, "Gain");
    r2Treble = &addKnob (params::r2Treble, "Treble");
    r2Master = &addKnob (params::r2Master, "Master");
    r2Presence = &addKnob (params::r2Presence, "Presence");
    ldGain = &addKnob (params::leadGain, "Gain");
    ldTreble = &addKnob (params::leadTreble, "Treble");
    ldBass = &addKnob (params::leadBass, "Bass");
    ldMid = &addKnob (params::leadMid, "Mid");
    ldDrive = &addKnob (params::leadDrive, "Drive");
    ldMaster = &addKnob (params::leadMaster, "Master");
    ldPresence = &addKnob (params::leadPresence, "Presence");
    outLevel = &addKnob (params::outputLevel, "Output Level");
    inTrim = &addKnob (params::inputTrim, "Input");
    outTrim = &addKnob (params::outputTrim, "Output");

    const char* eqLabels[5] = { "80", "240", "750", "2200", "6600" };
    for (int b = 0; b < 5; ++b)
        eqSliders[(size_t) b] = &addEqSlider (eqBands[b], eqLabels[b]);

    r1Bright = &addToggle (params::r1Bright, "Pull Bright");
    r2Fat = &addToggle (params::r2Fat, "Pull Fat");
    ldFat = &addToggle (params::leadFat, "Pull Fat");
    ldBright = &addToggle (params::leadBright, "Lead Bright");

    voicing = &addChoice (params::voicing, "Lead voicing");
    eqMode = &addChoice (params::eqMode, "EQ");
    cabMode = &addChoice (params::cabMode, "Cabinet");
    quality = &addChoice (params::quality, "Oversampling");

    const char* channelNames[3] = { "RHYTHM 1", "RHYTHM 2", "LEAD" };
    for (int c = 0; c < 3; ++c)
    {
        auto& b = channelButtons[(size_t) c];
        b.setButtonText (channelNames[c]);
        b.setClickingTogglesState (false);
        b.onClick = [this, c]
        {
            if (auto* param = processor.apvts.getParameter (params::channel))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) c));
        };
        addAndMakeVisible (b);
    }

    loadIrButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a cabinet impulse response",
                                                       juce::File(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                  {
                                      processor.loadImpulseResponse (file);
                                      if (auto* param = processor.apvts.getParameter (params::cabMode))
                                          param->setValueNotifyingHost (param->convertTo0to1 (2.0f));
                                  }
                              });
    };
    addAndMakeVisible (loadIrButton);
    irLabel.setJustificationType (juce::Justification::centredLeft);
    irLabel.setColour (juce::Label::textColourId, kText.withAlpha (0.7f));
    addAndMakeVisible (irLabel);

    setSize (kWidth, kHeight);
    startTimerHz (20);
    timerCallback();
}

MarkIVEditor::~MarkIVEditor()
{
    setLookAndFeel (nullptr);
}

MarkIVEditor::Knob& MarkIVEditor::addKnob (const char* paramId, const juce::String& text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attachment = std::make_unique<APVTS::SliderAttachment> (processor.apvts, paramId, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

MarkIVEditor::Knob& MarkIVEditor::addEqSlider (const char* paramId, const juce::String& text)
{
    auto& k = addKnob (paramId, text);
    k.slider.setSliderStyle (juce::Slider::LinearVertical);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 16);
    k.slider.setDoubleClickReturnValue (true, 0.0);
    return k;
}

MarkIVEditor::Toggle& MarkIVEditor::addToggle (const char* paramId, const juce::String& text)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText (text);
    addAndMakeVisible (t->button);
    t->attachment = std::make_unique<APVTS::ButtonAttachment> (processor.apvts, paramId, t->button);
    toggles.push_back (std::move (t));
    return *toggles.back();
}

MarkIVEditor::Choice& MarkIVEditor::addChoice (const char* paramId, const juce::String& text)
{
    auto c = std::make_unique<Choice>();
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        c->box.addItemList (param->choices, 1);
    c->label.setText (text, juce::dontSendNotification);
    c->label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (c->box);
    addAndMakeVisible (c->label);
    c->attachment = std::make_unique<APVTS::ComboBoxAttachment> (processor.apvts, paramId, c->box);
    choices.push_back (std::move (c));
    return *choices.back();
}

void MarkIVEditor::timerCallback()
{
    const int ch = (int) processor.apvts.getRawParameterValue (params::channel)->load();
    for (int c = 0; c < 3; ++c)
        channelButtons[(size_t) c].setToggleState (c == ch, juce::dontSendNotification);
    if (ch != lastChannel)
    {
        lastChannel = ch;
        repaint();
    }

    const auto name = processor.getImpulseResponseName();
    irLabel.setText (name.isEmpty() ? "No IR loaded" : name, juce::dontSendNotification);
}

void MarkIVEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    g.setColour (kAccent);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("MARK IV", 20, 10, 200, 32, juce::Justification::centredLeft);
    g.setColour (kText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("preamp model from the Mark IVB schematic", 150, 14, 300, 28, juce::Justification::centredLeft);

    const int ch = (int) processor.apvts.getRawParameterValue (params::channel)->load();
    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto& grp = groups[i];
        const bool active = (grp.title == "RHYTHM 1" && ch == 0) || (grp.title == "RHYTHM 2" && ch == 1)
                            || (grp.title == "LEAD" && ch == 2);
        g.setColour (kPanel);
        g.fillRoundedRectangle (grp.bounds.toFloat(), 6.0f);
        g.setColour (active ? (ch == 2 ? kLeadRed : kAccent) : juce::Colour (0xff35363b));
        g.drawRoundedRectangle (grp.bounds.toFloat().reduced (0.5f), 6.0f, active ? 2.0f : 1.0f);
        g.setColour (kText.withAlpha (0.85f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (grp.title, grp.bounds.getX() + 10, grp.bounds.getY() + 4, 200, 18, juce::Justification::centredLeft);
    }
}

void MarkIVEditor::layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> list)
{
    const int w = area.getWidth() / (int) list.size();
    for (auto* k : list)
    {
        auto cell = area.removeFromLeft (w).reduced (4, 0);
        k->label.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell);
    }
}

void MarkIVEditor::resized()
{
    groups.clear();
    auto area = getLocalBounds().reduced (12);

    // Header: channel selector and quality.
    auto header = area.removeFromTop (40);
    header.removeFromLeft (440);
    for (auto& b : channelButtons)
        b.setBounds (header.removeFromLeft (110).reduced (4, 4));
    header.removeFromLeft (20);
    quality->label.setBounds (header.removeFromLeft (80));
    quality->box.setBounds (header.removeFromLeft (80).reduced (0, 8));
    area.removeFromTop (6);

    auto group = [this] (juce::Rectangle<int> r, const juce::String& title)
    {
        groups.push_back ({ title, r });
        return r.reduced (8).withTrimmedTop (18);
    };

    const int rowH = 170;

    // Row 1: Rhythm 1, shared Bass/Mid, Rhythm 2.
    auto row1 = area.removeFromTop (rowH);
    {
        auto r1 = group (row1.removeFromLeft (420), "RHYTHM 1");
        r1Bright->button.setBounds (r1.removeFromBottom (22).removeFromLeft (110));
        layoutKnobs (r1, { r1Gain, r1Treble, r1Master, r1Presence });
        row1.removeFromLeft (8);
        auto shared = group (row1.removeFromLeft (170), "R1 + R2");
        shared.removeFromBottom (22);
        layoutKnobs (shared, { bass, mid });
        row1.removeFromLeft (8);
        auto r2 = group (row1, "RHYTHM 2");
        r2Fat->button.setBounds (r2.removeFromBottom (22).removeFromLeft (110));
        layoutKnobs (r2, { r2Gain, r2Treble, r2Master, r2Presence });
    }
    area.removeFromTop (8);

    // Row 2: Lead.
    auto row2 = area.removeFromTop (rowH);
    {
        auto ld = group (row2, "LEAD");
        auto switches = ld.removeFromBottom (24);
        ldFat->button.setBounds (switches.removeFromLeft (110));
        ldBright->button.setBounds (switches.removeFromLeft (120));
        switches.removeFromLeft (20);
        voicing->label.setBounds (switches.removeFromLeft (90));
        voicing->box.setBounds (switches.removeFromLeft (130));
        layoutKnobs (ld, { ldGain, ldTreble, ldBass, ldMid, ldDrive, ldMaster, ldPresence });
    }
    area.removeFromTop (8);

    // Row 3: graphic EQ, output, cabinet.
    auto row3 = area;
    {
        auto eqArea = group (row3.removeFromLeft (400), "GRAPHIC EQ");
        auto modeRow = eqArea.removeFromBottom (24);
        eqMode->label.setBounds (modeRow.removeFromLeft (30));
        eqMode->box.setBounds (modeRow.removeFromLeft (110));
        const int w = eqArea.getWidth() / 5;
        for (auto* k : eqSliders)
        {
            auto cell = eqArea.removeFromLeft (w).reduced (6, 0);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
        }
        row3.removeFromLeft (8);

        auto outArea = group (row3.removeFromLeft (330), "OUTPUT");
        layoutKnobs (outArea, { outLevel, inTrim, outTrim });
        row3.removeFromLeft (8);

        auto cabArea = group (row3, "CABINET");
        cabMode->box.setBounds (cabArea.removeFromTop (26));
        cabArea.removeFromTop (10);
        loadIrButton.setBounds (cabArea.removeFromTop (28));
        cabArea.removeFromTop (6);
        irLabel.setBounds (cabArea.removeFromTop (22));
        cabMode->label.setVisible (false);
    }
    repaint();
}
