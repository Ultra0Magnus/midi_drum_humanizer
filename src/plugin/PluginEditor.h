#pragma once

#include "HitMonitor.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

class MidiDrumHumanizerEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit MidiDrumHumanizerEditor (MidiDrumHumanizerProcessor&);
    ~MidiDrumHumanizerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct GroupRow
    {
        std::array<juce::Slider, 3> sliders; // velocity, timing, shift
        std::array<std::unique_ptr<SliderAttachment>, 3> attachments;
        juce::Rectangle<int> labelArea;
    };

    void timerCallback() override;
    void setUpKnob (Knob&, const juce::String& parameterId, const juce::String& name, const juce::String& tooltip, bool bipolar);
    void setUpBar (juce::Slider&, std::unique_ptr<SliderAttachment>&, const juce::String& parameterId,
                   const juce::String& tooltip, bool bipolar, juce::Colour);
    void enableDoubleClickReset (juce::Slider&, const juce::String& parameterId);
    void rollNewTake();

    static void layoutKnob (Knob&, juce::Rectangle<int> area);
    static void drawPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);

    MidiDrumHumanizerProcessor& processor;
    ui::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    enum KnobIndex { amountKnob, timingKnob, velocityKnob, accentKnob, driftKnob, feelKnob, numKnobs };
    std::array<Knob, numKnobs> knobs;

    std::array<GroupRow, dh::NumGroups> rows;
    std::array<juce::Label, 3> columnHeaders;
    ui::HitMonitor hitMonitor;

    juce::ComboBox presetBox;
    juce::Label variationLabel;
    juce::Slider variationSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    std::unique_ptr<SliderAttachment> variationAttachment;
    juce::TextButton newTakeButton { "New take" };
    juce::ToggleButton repeatableToggle { "Same every playback" };
    juce::ToggleButton lookaheadToggle { "Allow early hits (40 ms lookahead)" };
    std::unique_ptr<ButtonAttachment> repeatableAttachment, lookaheadAttachment;

    juce::Rectangle<int> titleArea, knobPanel, groupPanel, monitorPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDrumHumanizerEditor)
};
