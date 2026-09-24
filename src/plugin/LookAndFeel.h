#pragma once

#include "DrumMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{

namespace colours
{
    inline const juce::Colour background   { 0xff15171c };
    inline const juce::Colour panel        { 0xff1e2128 };
    inline const juce::Colour panelOutline { 0xff2c313b };
    inline const juce::Colour track        { 0xff30353f };
    inline const juce::Colour text         { 0xffe8eaef };
    inline const juce::Colour dimText      { 0xff8b919d };
    inline const juce::Colour accent       { 0xffffa53d };

    juce::Colour forGroup (dh::DrumGroup group);
} // namespace colours

// Sliders with the "bipolar" property set draw their value from the centre.
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosition,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosition,
                           float minSliderPosition, float maxSliderPosition, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
};

} // namespace ui
