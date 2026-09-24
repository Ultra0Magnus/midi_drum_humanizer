#pragma once

#include "HumanizerEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{

// Scatter plot of the latest hits: timing change (x) against the new velocity (y).
class HitMonitor final : public juce::Component
{
public:
    void addHit (const dh::HitInfo& hit, double nowMs);
    void setTime (double nowMs);
    void paint (juce::Graphics&) override;

private:
    struct Dot
    {
        float offsetMs;
        int velocity;
        dh::DrumGroup group;
        double time;
    };

    static constexpr int maxDots = 384;
    static constexpr double lifetimeMs = 4000.0;
    static constexpr float rangeMs = 40.0f;

    juce::Rectangle<float> getPlotArea() const;

    std::array<Dot, maxDots> dots {};
    int nextDot = 0;
    int numDots = 0;
    double now = 0.0;
    double lastHitTime = -lifetimeMs;
};

} // namespace ui
