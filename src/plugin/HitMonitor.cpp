#include "HitMonitor.h"
#include "LookAndFeel.h"

namespace ui
{

void HitMonitor::addHit (const dh::HitInfo& hit, double nowMs)
{
    dots[(size_t) nextDot] = { hit.offsetMs, hit.velocityOut, hit.group, nowMs };
    nextDot = (nextDot + 1) % maxDots;
    numDots = juce::jmin (numDots + 1, maxDots);
    lastHitTime = nowMs;
}

void HitMonitor::setTime (double nowMs)
{
    const bool wasAnimating = now - lastHitTime < lifetimeMs;
    now = nowMs;

    if (wasAnimating)
        repaint();
}

juce::Rectangle<float> HitMonitor::getPlotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (28.0f).withTrimmedBottom (18.0f).withTrimmedTop (4.0f).withTrimmedRight (6.0f);
}

void HitMonitor::paint (juce::Graphics& g)
{
    const auto plot = getPlotArea();

    g.setColour (colours::background.withAlpha (0.6f));
    g.fillRoundedRectangle (plot, 4.0f);

    const auto xFor = [&] (float ms) { return plot.getCentreX() + juce::jlimit (-1.0f, 1.0f, ms / rangeMs) * plot.getWidth() * 0.5f; };
    const auto yFor = [&] (float velocity) { return plot.getBottom() - velocity / 127.0f * plot.getHeight(); };

    g.setFont (juce::FontOptions (10.5f));

    for (const auto ms : { -30.0f, -20.0f, -10.0f, 10.0f, 20.0f, 30.0f })
    {
        g.setColour (colours::panelOutline);
        g.drawVerticalLine (juce::roundToInt (xFor (ms)), plot.getY(), plot.getBottom());
        g.setColour (colours::dimText);
        g.drawText ((ms > 0 ? "+" : "") + juce::String ((int) ms), juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ xFor (ms), plot.getBottom() + 9.0f }),
                    juce::Justification::centred);
    }

    for (const auto velocity : { 32, 64, 96, 127 })
    {
        g.setColour (colours::panelOutline);
        g.drawHorizontalLine (juce::roundToInt (yFor ((float) velocity)), plot.getX(), plot.getRight());
        g.setColour (colours::dimText);
        g.drawText (juce::String (velocity), juce::Rectangle<float> (0.0f, yFor ((float) velocity) - 7.0f, 24.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    g.setColour (colours::dimText.withAlpha (0.8f));
    g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());
    g.drawText ("grid", juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ plot.getCentreX(), plot.getBottom() + 9.0f }), juce::Justification::centred);
    g.drawText ("early", plot.withHeight (16.0f).reduced (6.0f, 0.0f), juce::Justification::centredLeft);
    g.drawText ("late", plot.withHeight (16.0f).reduced (6.0f, 0.0f), juce::Justification::centredRight);

    bool anyVisible = false;

    for (int i = 0; i < numDots; ++i)
    {
        const auto& dot = dots[(size_t) i];
        const auto age = now - dot.time;

        if (age < 0.0 || age > lifetimeMs)
            continue;

        anyVisible = true;
        const auto alpha = (float) (1.0 - age / lifetimeMs);
        const auto size = 4.0f + 4.0f * juce::jmax (0.0f, 1.0f - (float) age / 250.0f); // freshly played hits pop a little

        g.setColour (colours::forGroup (dot.group).withAlpha (alpha * 0.9f));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre ({ xFor (dot.offsetMs), yFor ((float) dot.velocity) }));
    }

    if (! anyVisible)
    {
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Play your drum track to see the hits", plot, juce::Justification::centred);
    }
}

} // namespace ui
