#include "LookAndFeel.h"

namespace ui
{

juce::Colour colours::forGroup (dh::DrumGroup group)
{
    switch (group)
    {
        case dh::Kick:     return juce::Colour (0xffff6b6b);
        case dh::Snare:    return juce::Colour (0xfffeca57);
        case dh::HatsRide: return juce::Colour (0xff48dbfb);
        case dh::Toms:     return juce::Colour (0xff1dd1a1);
        case dh::Cymbals:  return juce::Colour (0xffa29bfe);
        case dh::Other:
        case dh::NumGroups:
        default:           return juce::Colour (0xffc8d6e5);
    }
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);

    setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour (juce::Slider::trackColourId, colours::accent);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.4f));

    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::Label::textWhenEditingColourId, colours::text);
    setColour (juce::Label::outlineWhenEditingColourId, colours::accent);
    setColour (juce::TextEditor::backgroundColourId, colours::panel);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, colours::accent);

    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::track);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::text);

    setColour (juce::ComboBox::backgroundColourId, colours::track);
    setColour (juce::ComboBox::outlineColourId, colours::panelOutline);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);

    setColour (juce::TooltipWindow::backgroundColourId, colours::panel.brighter (0.15f));
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::accent.withAlpha (0.5f));
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPosition,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto lineWidth = juce::jmax (3.0f, radius * 0.12f);
    const auto arcRadius = radius - lineWidth * 0.5f;
    const juce::PathStrokeType stroke (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, stroke);

    const bool bipolar = slider.getProperties()["bipolar"];
    const auto valueAngle = startAngle + sliderPosition * (endAngle - startAngle);
    const auto fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.4f);

    if (std::abs (valueAngle - fromAngle) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (fromAngle, valueAngle), juce::jmax (fromAngle, valueAngle), true);
        g.setColour (colour);
        g.strokePath (value, stroke);
    }

    const auto knobRadius = arcRadius - lineWidth * 1.5f;
    const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (colours::panel.brighter (0.35f), centre.x, knob.getY(),
                                             colours::panel.darker (0.4f), centre.x, knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (colours::panelOutline.brighter (0.3f));
    g.drawEllipse (knob, 1.0f);

    g.setColour (colours::text);
    g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.35f, valueAngle),
                  centre.getPointOnCircumference (knobRadius * 0.85f, valueAngle) },
                juce::jmax (2.0f, radius * 0.06f));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPosition,
                                    float minSliderPosition, float maxSliderPosition, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPosition, minSliderPosition, maxSliderPosition, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (0.5f, 2.0f);
    constexpr float corner = 4.0f;

    g.setColour (colours::track);
    g.fillRoundedRectangle (bounds, corner);

    const bool bipolar = slider.getProperties()["bipolar"];
    const auto zero = bipolar ? (float) slider.getPositionOfValue (0.0) : bounds.getX();
    const auto left = juce::jlimit (bounds.getX(), bounds.getRight(), juce::jmin (zero, sliderPosition));
    const auto right = juce::jlimit (bounds.getX(), bounds.getRight(), juce::jmax (zero, sliderPosition));
    const auto colour = slider.findColour (juce::Slider::trackColourId);

    {
        juce::Graphics::ScopedSaveState saved (g);
        juce::Path shape;
        shape.addRoundedRectangle (bounds, corner);
        g.reduceClipRegion (shape);

        g.setColour (colour.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, bounds.getY(), right, bounds.getBottom()));

        if (right - left > 0.5f)
        {
            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (2.0f, bounds.getHeight()).withCentre ({ sliderPosition, bounds.getCentreY() }));
        }

        if (bipolar)
        {
            g.setColour (colours::dimText.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (zero - 0.5f, bounds.getY(), 1.0f, 3.0f));
            g.fillRect (juce::Rectangle<float> (zero - 0.5f, bounds.getBottom() - 3.0f, 1.0f, 3.0f));
        }
    }

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (slider.getTextFromValue (slider.getValue()), bounds, juce::Justification::centred, false);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    constexpr float switchWidth = 30.0f, switchHeight = 16.0f;
    const auto switchBounds = juce::Rectangle<float> (switchWidth, switchHeight)
                                  .withPosition (bounds.getX() + 2.0f, bounds.getCentreY() - switchHeight * 0.5f);
    const bool on = button.getToggleState();

    g.setColour (on ? colours::accent : colours::track.brighter (highlighted ? 0.2f : 0.0f));
    g.fillRoundedRectangle (switchBounds, switchHeight * 0.5f);

    const auto knobSize = switchHeight - 4.0f;
    const auto knobX = on ? switchBounds.getRight() - switchHeight * 0.5f : switchBounds.getX() + switchHeight * 0.5f;
    g.setColour (on ? colours::background : colours::dimText);
    g.fillEllipse (juce::Rectangle<float> (knobSize, knobSize).withCentre ({ knobX, switchBounds.getCentreY() }));

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (on ? 1.0f : 0.75f));
    g.setFont (juce::FontOptions (13.5f));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (switchWidth + 10.0f), juce::Justification::centredLeft, true);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                        bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = background;

    if (down)
        colour = colours::accent.withAlpha (0.45f);
    else if (highlighted)
        colour = colour.brighter (0.15f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (highlighted ? colours::accent.withAlpha (0.7f) : colours::panelOutline.brighter (0.2f));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

} // namespace ui
