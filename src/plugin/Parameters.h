#pragma once

#include "HumanizerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace params
{

inline constexpr auto amount     = "amount";
inline constexpr auto timing     = "timing";
inline constexpr auto velocity   = "velocity";
inline constexpr auto accent     = "accent";
inline constexpr auto drift      = "drift";
inline constexpr auto feel       = "feel";
inline constexpr auto seed       = "seed";
inline constexpr auto repeatable = "repeatable";
inline constexpr auto lookahead  = "lookahead";

enum class GroupParam { velocityAmount, timingAmount, shift };

juce::String groupParamId (dh::DrumGroup group, GroupParam which);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Reads the current parameter values into engine settings (audio thread safe).
class SettingsReader
{
public:
    explicit SettingsReader (juce::AudioProcessorValueTreeState& state);

    dh::Settings read() const noexcept;
    bool lookaheadEnabled() const noexcept { return lookahead->load() >= 0.5f; }

private:
    std::atomic<float>* amount;
    std::atomic<float>* timing;
    std::atomic<float>* velocity;
    std::atomic<float>* accent;
    std::atomic<float>* drift;
    std::atomic<float>* feel;
    std::atomic<float>* seed;
    std::atomic<float>* repeatable;
    std::atomic<float>* lookahead;

    struct GroupPointers
    {
        std::atomic<float>* velocity;
        std::atomic<float>* timing;
        std::atomic<float>* shift;
    };

    std::array<GroupPointers, dh::NumGroups> groups;
};

//==============================================================================
struct Preset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values; // overrides on top of the defaults
};

const std::vector<Preset>& factoryPresets();

// Sets every parameter to its default, then applies the preset's values (message thread).
void applyPreset (juce::AudioProcessorValueTreeState& state, const Preset& preset);

} // namespace params
