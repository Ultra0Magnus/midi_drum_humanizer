#include "Parameters.h"

namespace params
{

namespace
{
    constexpr int parameterVersion = 1;

    constexpr const char* groupKeys[dh::NumGroups] = { "kick", "snare", "hats", "toms", "cymbals", "other" };

    struct GroupDefaults
    {
        float velocity, timing, shift;
    };

    // Kick and snare are usually played more consistently than hi-hats.
    constexpr GroupDefaults groupDefaults[dh::NumGroups] = {
        {  80.0f,  70.0f, 0.0f }, // kick
        {  70.0f,  70.0f, 0.0f }, // snare
        { 120.0f, 100.0f, 0.0f }, // hi-hat / ride
        { 100.0f, 100.0f, 0.0f }, // toms
        {  80.0f, 100.0f, 0.0f }, // cymbals
        { 100.0f, 100.0f, 0.0f }, // other
    };

    juce::String formatMs (float value, bool showSign)
    {
        const auto rounded = std::abs (value) < 0.05f ? 0.0f : value;
        return (showSign && rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " ms";
    }

    juce::String formatPercent (float value) { return juce::String (juce::roundToInt (value)) + " %"; }

    float parseNumber (const juce::String& text) { return text.retainCharacters ("-+.0123456789").getFloatValue(); }

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                          juce::NormalisableRange<float> range, float defaultValue,
                                                          std::function<juce::String (float, int)> toText)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, parameterVersion }, name, range, defaultValue,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction (std::move (toText))
                                                                .withValueFromStringFunction (parseNumber));
    }
} // namespace

juce::String groupParamId (dh::DrumGroup group, GroupParam which)
{
    const juce::String key = groupKeys[(size_t) group];

    switch (which)
    {
        case GroupParam::velocityAmount: return key + "_velocity";
        case GroupParam::timingAmount:   return key + "_timing";
        case GroupParam::shift:          return key + "_shift";
    }

    return key;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto ms = [] (float v, int) { return formatMs (v, false); };
    const auto signedMs = [] (float v, int) { return formatMs (v, true); };
    const auto percent = [] (float v, int) { return formatPercent (v); };

    layout.add (makeFloat (amount, "Amount", { 0.0f, 100.0f, 0.1f }, 100.0f, percent),
                makeFloat (timing, "Timing", { 0.0f, 30.0f, 0.1f }, 8.0f, ms),
                makeFloat (velocity, "Velocity", { 0.0f, 40.0f, 0.1f }, 14.0f,
                           [] (float v, int) { return juce::String::charToString (0xb1) + juce::String (v, 1); }),
                makeFloat (accent, "Accents", { 0.0f, 100.0f, 0.1f }, 50.0f, percent),
                makeFloat (drift, "Drift", { 0.0f, 15.0f, 0.1f }, 3.0f, ms),
                makeFloat (feel, "Feel", { -25.0f, 25.0f, 0.1f }, 0.0f, signedMs));

    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { seed, parameterVersion }, "Variation", 1, 999, 1),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { repeatable, parameterVersion }, "Same Every Playback", true),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { lookahead, parameterVersion }, "Allow Early Hits", true));

    for (int g = 0; g < dh::NumGroups; ++g)
    {
        const auto group = (dh::DrumGroup) g;
        const juce::String name = dh::groupName (group);
        const auto& defaults = groupDefaults[g];

        layout.add (makeFloat (groupParamId (group, GroupParam::velocityAmount), name + " Velocity", { 0.0f, 200.0f, 1.0f }, defaults.velocity, percent),
                    makeFloat (groupParamId (group, GroupParam::timingAmount), name + " Timing", { 0.0f, 200.0f, 1.0f }, defaults.timing, percent),
                    makeFloat (groupParamId (group, GroupParam::shift), name + " Shift", { -20.0f, 20.0f, 0.1f }, defaults.shift, signedMs));
    }

    return layout;
}

//==============================================================================
SettingsReader::SettingsReader (juce::AudioProcessorValueTreeState& state)
    : amount (state.getRawParameterValue (params::amount)),
      timing (state.getRawParameterValue (params::timing)),
      velocity (state.getRawParameterValue (params::velocity)),
      accent (state.getRawParameterValue (params::accent)),
      drift (state.getRawParameterValue (params::drift)),
      feel (state.getRawParameterValue (params::feel)),
      seed (state.getRawParameterValue (params::seed)),
      repeatable (state.getRawParameterValue (params::repeatable)),
      lookahead (state.getRawParameterValue (params::lookahead))
{
    for (int g = 0; g < dh::NumGroups; ++g)
    {
        const auto group = (dh::DrumGroup) g;
        groups[(size_t) g] = { state.getRawParameterValue (groupParamId (group, GroupParam::velocityAmount)),
                               state.getRawParameterValue (groupParamId (group, GroupParam::timingAmount)),
                               state.getRawParameterValue (groupParamId (group, GroupParam::shift)) };
        jassert (groups[(size_t) g].velocity != nullptr && groups[(size_t) g].timing != nullptr && groups[(size_t) g].shift != nullptr);
    }
}

dh::Settings SettingsReader::read() const noexcept
{
    dh::Settings s;
    s.amount        = amount->load() / 100.0f;
    s.timingMs      = timing->load();
    s.velocityRange = velocity->load();
    s.accent        = accent->load() / 100.0f;
    s.driftMs       = drift->load();
    s.feelMs        = feel->load();
    s.seed          = (uint32_t) juce::jmax (1, juce::roundToInt (seed->load()));
    s.repeatable    = repeatable->load() >= 0.5f;

    for (size_t g = 0; g < groups.size(); ++g)
        s.groups[g] = { groups[g].velocity->load() / 100.0f, groups[g].timing->load() / 100.0f, groups[g].shift->load() };

    return s;
}

//==============================================================================
const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets = {
        { "Natural", {} },
        { "Subtle", { { timing, 4.0f }, { velocity, 8.0f }, { accent, 35.0f }, { drift, 1.5f } } },
        { "Tight Studio Drummer", { { timing, 5.0f }, { velocity, 10.0f }, { accent, 60.0f }, { drift, 1.0f },
                                    { groupParamId (dh::Kick, GroupParam::timingAmount), 50.0f },
                                    { groupParamId (dh::Snare, GroupParam::timingAmount), 50.0f } } },
        { "Laid Back Groove", { { accent, 55.0f }, { feel, 6.0f }, { groupParamId (dh::Snare, GroupParam::shift), 5.0f } } },
        { "Pushing / Energetic", { { timing, 7.0f }, { velocity, 16.0f }, { accent, 55.0f }, { drift, 2.0f }, { feel, -5.0f },
                                   { groupParamId (dh::HatsRide, GroupParam::shift), -2.0f } } },
        { "Loose Jam", { { timing, 15.0f }, { velocity, 22.0f }, { accent, 60.0f }, { drift, 6.0f } } },
        { "Sloppy Garage", { { timing, 26.0f }, { velocity, 30.0f }, { accent, 40.0f }, { drift, 10.0f } } },
        { "Velocity Only", { { timing, 0.0f }, { drift, 0.0f }, { velocity, 16.0f }, { accent, 55.0f } } },
        { "Timing Only", { { velocity, 0.0f }, { accent, 0.0f } } },
    };

    return presets;
}

void applyPreset (juce::AudioProcessorValueTreeState& state, const Preset& preset)
{
    // Presets change the feel, not the performance: keep the variation and the switches.
    static const juce::StringArray keep { seed, repeatable, lookahead };

    const auto setValue = [&state] (const juce::String& id, float value)
    {
        if (auto* parameter = state.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            parameter->endChangeGesture();
        }
    };

    for (auto* parameter : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! keep.contains (ranged->getParameterID()))
                setValue (ranged->getParameterID(), ranged->convertFrom0to1 (ranged->getDefaultValue()));

    for (const auto& [id, value] : preset.values)
        setValue (id, value);
}

} // namespace params
