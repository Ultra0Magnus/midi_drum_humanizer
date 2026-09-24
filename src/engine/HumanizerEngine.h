#pragma once

#include "DrumMap.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dh
{

struct GroupSettings
{
    float velocityScale = 1.0f; // multiplier applied to the random velocity range
    float timingScale   = 1.0f; // multiplier applied to the random timing range
    float shiftMs       = 0.0f; // constant timing offset for this drum group
};

struct Settings
{
    float amount        = 1.0f;  // master intensity of the random parts, 0..1
    float timingMs      = 8.0f;  // maximum random timing deviation (ms)
    float velocityRange = 14.0f; // maximum random velocity deviation (MIDI velocity steps)
    float accent        = 0.5f;  // strength of the beat-position accents, 0..1
    float driftMs       = 3.0f;  // slow timing wander (ms)
    float feelMs        = 0.0f;  // constant offset: negative = ahead of the beat, positive = laid back
    uint32_t seed       = 1;     // selects the (repeatable) performance
    bool repeatable     = true;  // same performance on every playback
    std::array<GroupSettings, NumGroups> groups {};
};

// Host transport state at the start of a block.
struct Transport
{
    bool isPlaying          = false;
    bool hasPpq             = false;
    double ppqAtBlockStart  = 0.0;
    double bpm              = 120.0;
    bool hasBarStart        = false;
    double ppqOfLastBarStart = 0.0;
    int timeSigNumerator    = 4;
    int timeSigDenominator  = 4;
};

// A short (up to 3 byte) MIDI message at a sample position inside the current block.
struct MidiEvent
{
    int sampleOffset = 0;
    uint8_t data[3] {};
    uint8_t size = 0;
};

// Describes what happened to a note-on, used by the plugin's hit monitor.
struct HitInfo
{
    int note        = 0;
    int channel     = 0;
    int velocityIn  = 0;
    int velocityOut = 0;
    float offsetMs  = 0.0f; // timing change relative to the original position
    DrumGroup group = Other;
};

enum class GridPosition
{
    Downbeat,     // first beat of the bar
    Beat,         // other beats
    Eighth,       // the "&" between beats
    Sixteenth,    // the "e" and "a"
    Triplet,
    ThirtySecond,
    OffGrid,
    NumPositions
};

GridPosition classifyGridPosition (double ppq, const Transport& transport) noexcept;

// Velocity change for a hit of this group on this grid position, at 100 % accent strength.
float accentFor (DrumGroup group, GridPosition position) noexcept;

//==============================================================================
/*  Real-time MIDI drum humanizer.

    Note-ons get a new velocity and a new position in time. Notes can only be moved
    earlier than their original position if a lookahead is set: every event is then
    delayed by the lookahead (which the plugin reports to the host as latency), and
    individual hits are moved within that window.

    Guarantees:
      - every event is delayed by at least 0 and the lookahead is never exceeded early,
      - note-offs keep the duration of their note-on and always follow it,
      - consecutive hits on the same drum never swap order,
      - with "repeatable" enabled the result depends only on the musical position of a
        note, so every playback and render produces exactly the same performance.

    Usage per audio block: beginBlock(), pushEvent() for every input event (in time
    order), popDueEvents(), endBlock().
*/
class HumanizerEngine
{
public:
    static constexpr int maxPendingEvents = 4096;
    static constexpr double maxLateMs = 100.0;
    static constexpr double minVelocityHeadroom = 8.0;

    HumanizerEngine();

    void prepare (double sampleRate, int lookaheadSamples);
    void reset();

    void setLookaheadSamples (int lookaheadSamples) noexcept;
    int getLookaheadSamples() const noexcept { return lookahead; }

    void setSettings (const Settings& newSettings) noexcept { settings = newSettings; }
    const Settings& getSettings() const noexcept { return settings; }

    void beginBlock (int numSamples, const Transport& transport) noexcept;

    // Returns true (and fills hit, if given) when the event was a note-on.
    bool pushEvent (const MidiEvent& event, HitInfo* hit = nullptr) noexcept;

    // Calls callback (const MidiEvent&) for every event due in the current block,
    // in time order, with sampleOffset relative to the block start.
    template <typename Callback>
    void popDueEvents (Callback&& callback)
    {
        const auto blockEnd = blockStart + blockLength;
        size_t numDue = 0;

        for (; numDue < pending.size() && pending[numDue].time < blockEnd; ++numDue)
        {
            auto event = pending[numDue].event;
            event.sampleOffset = (int) (pending[numDue].time > blockStart ? pending[numDue].time - blockStart : 0);
            callback (static_cast<const MidiEvent&> (event));
        }

        pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) numDue);
    }

    void endBlock() noexcept;

    size_t getNumPendingEvents() const noexcept { return pending.size(); }

private:
    enum Priority : int
    {
        noteOffPriority = 0, // note-offs first, so a re-triggered note is never cut
        otherPriority   = 1, // controllers before notes (e.g. hi-hat pedal position)
        noteOnPriority  = 2
    };

    struct PendingEvent
    {
        int64_t time;
        int priority;
        uint64_t order;
        MidiEvent event;
    };

    struct KeyState
    {
        bool hasNoteOn = false;
        bool gapValid = false;
        int64_t lastOnIn = 0;
        int64_t lastOnOut = 0;
        int64_t lastDelay = 0;
    };

    static bool comesBefore (const PendingEvent&, const PendingEvent&) noexcept;
    bool scheduleNoteOn (const MidiEvent&, int64_t inputTime, HitInfo*) noexcept;
    void scheduleNoteOff (const MidiEvent&, int64_t inputTime) noexcept;
    void insert (int64_t time, int priority, const MidiEvent&) noexcept;
    void moveNoteOffsTo (int channel, int note, int64_t time) noexcept;
    int64_t latestPendingNoteTime (int channel, int64_t fallback) const noexcept;
    double driftAt (uint64_t seed, double beats) const noexcept;
    uint64_t currentSeed() const noexcept;
    KeyState& keyState (int channel, int note) noexcept { return keys[(size_t) (channel * 128 + note)]; }

    Settings settings;
    Transport transport;
    double sampleRate = 44100.0;
    int lookahead = 0;

    int64_t blockStart = 0;
    int blockLength = 0;

    bool lastBlockUsedPpq = false;
    double expectedPpq = 0.0;
    uint64_t performanceSeed = 0;
    uint64_t seedCounter = 0;
    uint64_t liveCounter = 0;
    uint64_t nextOrder = 0;

    std::vector<PendingEvent> pending;
    std::array<KeyState, 16 * 128> keys {};
};

} // namespace dh
