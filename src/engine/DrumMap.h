#pragma once

namespace dh
{

// Drum groups get their own humanization amounts and accent profiles.
enum DrumGroup : int
{
    Kick = 0,
    Snare,
    HatsRide,
    Toms,
    Cymbals,
    Other,
    NumGroups
};

// General MIDI drum map (used by most drum libraries: EZdrummer, Superior Drummer,
// Addictive Drums, SSD, MT Power Drum Kit, ReaSamplOmatic presets, ...).
constexpr DrumGroup groupForNote (int note) noexcept
{
    switch (note)
    {
        case 35: case 36:                                   // acoustic / bass drum 1
            return Kick;

        case 37: case 38: case 40:                          // side stick, snare, electric snare
            return Snare;

        case 42: case 44: case 46:                          // closed, pedal, open hi-hat
        case 51: case 53: case 59:                          // ride 1, ride bell, ride 2
            return HatsRide;

        case 41: case 43: case 45: case 47: case 48: case 50:
            return Toms;

        case 49: case 52: case 55: case 57:                 // crash 1, china, splash, crash 2
            return Cymbals;

        default:
            return Other;
    }
}

constexpr const char* groupName (DrumGroup group) noexcept
{
    switch (group)
    {
        case Kick:     return "Kick";
        case Snare:    return "Snare";
        case HatsRide: return "Hi-Hat / Ride";
        case Toms:     return "Toms";
        case Cymbals:  return "Cymbals";
        case Other:
        case NumGroups:
        default:       return "Other";
    }
}

} // namespace dh
