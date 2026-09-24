#include "PluginEditor.h"

namespace
{
    constexpr int panelTitleHeight = 20;
    constexpr int panelPadding = 12;
}

MidiDrumHumanizerEditor::MidiDrumHumanizerEditor (MidiDrumHumanizerProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setUpKnob (knobs[amountKnob], params::amount, "Amount",
               "Master intensity of the humanization: scales timing, velocity, accents and drift together.", false);
    setUpKnob (knobs[timingKnob], params::timing, "Timing",
               "Maximum random timing deviation. Most hits land within a third of it, like a real drummer.", false);
    setUpKnob (knobs[velocityKnob], params::velocity, "Velocity",
               "Maximum random velocity deviation, in MIDI velocity steps.", false);
    setUpKnob (knobs[accentKnob], params::accent, "Accents",
               "Beat-aware dynamics: downbeats and backbeats get stronger, off-beat 16ths softer. Strongest on hi-hats and ride.", false);
    setUpKnob (knobs[driftKnob], params::drift, "Drift",
               "Slow, smooth timing wander over a couple of beats, as if the drummer breathes with the groove.", false);
    setUpKnob (knobs[feelKnob], params::feel, "Feel",
               "Moves every hit ahead of the beat (negative: pushing) or behind it (positive: laid back).", true);

    const juce::StringArray headers { "Velocity", "Timing", "Shift" };
    for (size_t i = 0; i < columnHeaders.size(); ++i)
    {
        columnHeaders[i].setText (headers[(int) i], juce::dontSendNotification);
        columnHeaders[i].setJustificationType (juce::Justification::centred);
        columnHeaders[i].setColour (juce::Label::textColourId, ui::colours::dimText);
        columnHeaders[i].setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (columnHeaders[i]);
    }

    for (int g = 0; g < dh::NumGroups; ++g)
    {
        const auto group = (dh::DrumGroup) g;
        const auto colour = ui::colours::forGroup (group);
        auto& row = rows[(size_t) g];

        setUpBar (row.sliders[0], row.attachments[0], params::groupParamId (group, params::GroupParam::velocityAmount),
                  "How much velocity randomness this drum gets (100 % = the Velocity knob).", false, colour);
        setUpBar (row.sliders[1], row.attachments[1], params::groupParamId (group, params::GroupParam::timingAmount),
                  "How much timing randomness this drum gets (100 % = the Timing knob).", false, colour);
        setUpBar (row.sliders[2], row.attachments[2], params::groupParamId (group, params::GroupParam::shift),
                  "Constant timing offset for this drum, e.g. a slightly late snare for a laid-back backbeat.", true, colour);
    }

    addAndMakeVisible (hitMonitor);

    const auto& presets = params::factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        presetBox.addItem (presets[i].name, (int) i + 1);

    presetBox.setTextWhenNothingSelected ("Load a preset...");
    presetBox.setTooltip ("Starting points for different drummers. Your variation number is kept.");
    presetBox.onChange = [this]
    {
        const auto index = presetBox.getSelectedItemIndex();
        if (index < 0)
            return;

        const auto& preset = params::factoryPresets()[(size_t) index];
        params::applyPreset (processor.getState(), preset);

        // Show the name without keeping the item selected, so the same preset can be picked again.
        presetBox.setText ("Preset: " + juce::String (preset.name), juce::dontSendNotification);
    };
    addAndMakeVisible (presetBox);

    variationLabel.setText ("Variation", juce::dontSendNotification);
    variationLabel.setFont (juce::FontOptions (13.5f));
    variationLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (variationLabel);

    variationSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 24);
    variationSlider.setColour (juce::Slider::textBoxBackgroundColourId, ui::colours::track);
    variationSlider.setColour (juce::Slider::textBoxOutlineColourId, ui::colours::panelOutline);
    variationSlider.setTooltip ("Picks a different, but repeatable, performance. The same number always plays the same way.");
    variationAttachment = std::make_unique<SliderAttachment> (processor.getState(), params::seed, variationSlider);
    addAndMakeVisible (variationSlider);

    newTakeButton.setTooltip ("Roll the dice: pick a new random variation.");
    newTakeButton.onClick = [this] { rollNewTake(); };
    addAndMakeVisible (newTakeButton);

    repeatableToggle.setTooltip ("On: every playback and render gives exactly the same humanized performance.\n"
                                 "Off: each playback is a new take.");
    repeatableAttachment = std::make_unique<ButtonAttachment> (processor.getState(), params::repeatable, repeatableToggle);
    addAndMakeVisible (repeatableToggle);

    lookaheadToggle.setTooltip ("Lets hits land slightly before the beat, not only after it. Adds 40 ms of latency, "
                                "which REAPER compensates automatically. Turn it off while recording live.");
    lookaheadAttachment = std::make_unique<ButtonAttachment> (processor.getState(), params::lookahead, lookaheadToggle);
    addAndMakeVisible (lookaheadToggle);

    // Hits played while the editor was closed are old news.
    processor.readHits ([] (const dh::HitInfo&) {});

    // Set last, so every child (including the slider text boxes) picks it up.
    setLookAndFeel (&lookAndFeel);

    setSize (820, 540);
    startTimerHz (30);
}

MidiDrumHumanizerEditor::~MidiDrumHumanizerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MidiDrumHumanizerEditor::setUpKnob (Knob& knob, const juce::String& parameterId, const juce::String& name,
                                         const juce::String& tooltip, bool bipolar)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    knob.slider.getProperties().set ("bipolar", bipolar);
    knob.slider.setTooltip (tooltip);
    knob.attachment = std::make_unique<SliderAttachment> (processor.getState(), parameterId, knob.slider);
    enableDoubleClickReset (knob.slider, parameterId);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    knob.label.setTooltip (tooltip);
    addAndMakeVisible (knob.label);
}

void MidiDrumHumanizerEditor::setUpBar (juce::Slider& slider, std::unique_ptr<SliderAttachment>& attachment,
                                        const juce::String& parameterId, const juce::String& tooltip, bool bipolar, juce::Colour colour)
{
    slider.setSliderStyle (juce::Slider::LinearBar);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setColour (juce::Slider::trackColourId, colour);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTooltip (tooltip + "\nDouble-click to reset.");
    attachment = std::make_unique<SliderAttachment> (processor.getState(), parameterId, slider);
    enableDoubleClickReset (slider, parameterId);
    addAndMakeVisible (slider);
}

void MidiDrumHumanizerEditor::enableDoubleClickReset (juce::Slider& slider, const juce::String& parameterId)
{
    if (auto* parameter = processor.getState().getParameter (parameterId))
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
}

void MidiDrumHumanizerEditor::rollNewTake()
{
    if (auto* parameter = processor.getState().getParameter (params::seed))
    {
        const auto current = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));
        auto next = current;

        while (next == current)
            next = juce::Random::getSystemRandom().nextInt ({ 1, 1000 });

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) next));
        parameter->endChangeGesture();
    }
}

void MidiDrumHumanizerEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    processor.readHits ([this, now] (const dh::HitInfo& hit) { hitMonitor.addHit (hit, now); });
    hitMonitor.setTime (now);
}

//==============================================================================
void MidiDrumHumanizerEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    const auto bounds = area.toFloat();
    g.setColour (ui::colours::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ui::colours::panelOutline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (ui::colours::dimText);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (title, area.reduced (panelPadding, 0).withTrimmedTop (8).withHeight (14), juce::Justification::centredLeft);
}

void MidiDrumHumanizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);

    auto title = titleArea;
    g.setColour (ui::colours::accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("MIDI DRUM HUMANIZER", title.removeFromTop (26), juce::Justification::bottomLeft);
    g.setColour (ui::colours::dimText);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("Turns robotic drum MIDI into a human performance", title, juce::Justification::topLeft);

    drawPanel (g, knobPanel, "GROOVE");
    drawPanel (g, groupPanel, "PER DRUM");
    drawPanel (g, monitorPanel, "HIT MONITOR");

    for (int i = 0; i < dh::NumGroups; ++i)
    {
        const auto group = (dh::DrumGroup) i;
        auto area = rows[(size_t) i].labelArea;

        g.setColour (ui::colours::forGroup (group));
        g.fillEllipse (area.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
        area.removeFromLeft (6);

        g.setColour (ui::colours::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (dh::groupName (group), area, juce::Justification::centredLeft);
    }
}

void MidiDrumHumanizerEditor::layoutKnob (Knob& knob, juce::Rectangle<int> area)
{
    knob.label.setBounds (area.removeFromTop (18));
    knob.slider.setBounds (area);
}

void MidiDrumHumanizerEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto header = area.removeFromTop (46);
    presetBox.setBounds (header.removeFromRight (240).withSizeKeepingCentre (240, 28));
    titleArea = header;

    area.removeFromTop (8);
    knobPanel = area.removeFromTop (178);
    area.removeFromTop (10);

    auto footer = area.removeFromBottom (36);
    area.removeFromBottom (10);

    groupPanel = area.removeFromLeft (440);
    area.removeFromLeft (10);
    monitorPanel = area;

    //--- global knobs
    auto knobArea = knobPanel.reduced (panelPadding).withTrimmedTop (panelTitleHeight - 6);
    layoutKnob (knobs[amountKnob], knobArea.removeFromLeft (150));
    knobArea.removeFromLeft (12);

    const int knobWidth = knobArea.getWidth() / (numKnobs - 1);
    for (int i = timingKnob; i < numKnobs; ++i)
        layoutKnob (knobs[(size_t) i], knobArea.removeFromLeft (knobWidth).withTrimmedTop (22).reduced (4, 0));

    //--- per drum table
    auto table = groupPanel.reduced (panelPadding).withTrimmedTop (panelTitleHeight - 4);
    auto headerRow = table.removeFromTop (18);
    constexpr int labelWidth = 118;
    headerRow.removeFromLeft (labelWidth);
    const int columnWidth = headerRow.getWidth() / (int) columnHeaders.size();

    for (auto& label : columnHeaders)
        label.setBounds (headerRow.removeFromLeft (columnWidth));

    const int rowHeight = table.getHeight() / dh::NumGroups;
    for (auto& row : rows)
    {
        auto rowArea = table.removeFromTop (rowHeight);
        row.labelArea = rowArea.removeFromLeft (labelWidth);

        for (auto& slider : row.sliders)
            slider.setBounds (rowArea.removeFromLeft (columnWidth).reduced (4, 2));
    }

    //--- hit monitor
    hitMonitor.setBounds (monitorPanel.reduced (panelPadding).withTrimmedTop (panelTitleHeight - 2));

    //--- footer
    variationLabel.setBounds (footer.removeFromLeft (66));
    footer.removeFromLeft (6);
    variationSlider.setBounds (footer.removeFromLeft (118).reduced (0, 4));
    footer.removeFromLeft (8);
    newTakeButton.setBounds (footer.removeFromLeft (86).reduced (0, 4));
    footer.removeFromLeft (24);
    repeatableToggle.setBounds (footer.removeFromLeft (196));
    footer.removeFromLeft (8);
    lookaheadToggle.setBounds (footer);
}
