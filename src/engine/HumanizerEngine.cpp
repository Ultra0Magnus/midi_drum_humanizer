#include "HumanizerEngine.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace dh
{

namespace
{
    constexpr uint64_t golden = 0x9E3779B97F4A7C15ull;

    constexpr uint64_t mix64 (uint64_t x) noexcept
    {
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    constexpr uint64_t combine (uint64_t hash, uint64_t value) noexcept
    {
        return mix64 (hash ^ (value + golden + (hash << 6) + (hash >> 2)));
    }

    // SplitMix64: tiny, fast and good enough to drive musical randomness.
    struct Rng
    {
        explicit Rng (uint64_t seed) noexcept : state (seed) {}

        uint64_t next() noexcept
        {
            state += golden;
            return mix64 (state);
        }

        double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

        // Bell shaped value in [-1, 1] (sum of three uniforms), standard deviation 1/3:
        // most hits stay close to the centre, a few go further out, none exceed the range.
        double bell() noexcept { return (uniform() + uniform() + uniform()) / 1.5 - 1.0; }

        uint64_t state;
    };

    constexpr uint64_t saltNote  = 0x4E4F5445ull; // "NOTE"
    constexpr uint64_t saltLive  = 0x4C495645ull; // "LIVE"
    constexpr uint64_t saltDrift = 0x44524654ull; // "DRFT"

    constexpr double ticksPerQuarter = 960.0;
    constexpr double driftPeriodBeats = 2.0;

    // Velocity accents at 100 % accent strength, indexed by [group][grid position].
    constexpr float accentTable[NumGroups][(int) GridPosition::NumPositions] = {
        //  Down   Beat  Eighth    16th  Triplet   32nd  OffGrid
        {   8.0f,  5.0f,  -3.0f,  -8.0f,  -5.0f, -10.0f,  -4.0f }, // Kick
        {   5.0f,  6.0f,  -4.0f, -12.0f,  -8.0f, -14.0f,  -6.0f }, // Snare
        {  12.0f,  8.0f,  -3.0f, -12.0f,  -7.0f, -16.0f,  -5.0f }, // Hi-hat / ride
        {   6.0f,  5.0f,  -2.0f,  -6.0f,  -4.0f,  -8.0f,  -3.0f }, // Toms
        {   8.0f,  4.0f,  -2.0f,  -4.0f,  -3.0f,  -5.0f,  -2.0f }, // Cymbals
        {   6.0f,  4.0f,  -2.0f,  -6.0f,  -4.0f,  -8.0f,  -3.0f }, // Other
    };

    bool isNear (double fraction, double target, double tolerance) noexcept
    {
        const auto distance = std::abs (fraction - target);
        return distance < tolerance || std::abs (distance - 1.0) < tolerance;
    }

    // Compresses a velocity change so that it approaches, but never exceeds, the headroom.
    double softLimit (double delta, double headroom) noexcept
    {
        return headroom > 0.0 ? headroom * std::tanh (delta / headroom) : 0.0;
    }

    bool isNoteOff (const MidiEvent& e) noexcept
    {
        const auto status = e.data[0] & 0xF0;
        return e.size >= 3 && (status == 0x80 || (status == 0x90 && e.data[2] == 0));
    }

    bool isNoteOn (const MidiEvent& e) noexcept
    {
        return e.size >= 3 && (e.data[0] & 0xF0) == 0x90 && e.data[2] > 0;
    }
} // namespace

//==============================================================================
GridPosition classifyGridPosition (double ppq, const Transport& t) noexcept
{
    const int numerator = std::max (1, t.timeSigNumerator);
    const int denominator = std::max (1, t.timeSigDenominator);
    const double barLength = numerator * 4.0 / denominator; // in quarter notes

    // 6/8, 9/8, 12/8...: the beat is a dotted quarter, subdivided in three.
    const bool compound = denominator >= 8 && numerator >= 6 && numerator % 3 == 0;
    const double beatLength = (compound ? 3.0 : 1.0) * 4.0 / denominator;

    double positionInBar = std::fmod (t.hasBarStart ? ppq - t.ppqOfLastBarStart : ppq, barLength);
    if (positionInBar < 0.0)
        positionInBar += barLength;

    const double beatPosition = positionInBar / beatLength;
    const double fraction = beatPosition - std::floor (beatPosition);
    const double tolerance = 0.03 / beatLength; // ~1/32 of a quarter note, for slightly unquantized input

    if (isNear (fraction, 0.0, tolerance))
    {
        const int beatsPerBar = std::max (1, (int) std::lround (barLength / beatLength));
        const int beatIndex = (int) std::floor (beatPosition + 0.5) % beatsPerBar;
        return beatIndex == 0 ? GridPosition::Downbeat : GridPosition::Beat;
    }

    if (compound)
    {
        if (isNear (fraction, 1.0 / 3.0, tolerance) || isNear (fraction, 2.0 / 3.0, tolerance))
            return GridPosition::Eighth;

        if (isNear (fraction, 1.0 / 6.0, tolerance) || isNear (fraction, 0.5, tolerance) || isNear (fraction, 5.0 / 6.0, tolerance))
            return GridPosition::Sixteenth;

        return GridPosition::OffGrid;
    }

    if (isNear (fraction, 0.5, tolerance))
        return GridPosition::Eighth;

    if (isNear (fraction, 0.25, tolerance) || isNear (fraction, 0.75, tolerance))
        return GridPosition::Sixteenth;

    if (isNear (fraction, 1.0 / 3.0, tolerance) || isNear (fraction, 2.0 / 3.0, tolerance))
        return GridPosition::Triplet;

    for (const auto target : { 0.125, 0.375, 0.625, 0.875 })
        if (isNear (fraction, target, tolerance))
            return GridPosition::ThirtySecond;

    return GridPosition::OffGrid;
}

float accentFor (DrumGroup group, GridPosition position) noexcept
{
    if (group < 0 || group >= NumGroups || position == GridPosition::NumPositions)
        return 0.0f;

    return accentTable[group][(int) position];
}

//==============================================================================
HumanizerEngine::HumanizerEngine()
{
    pending.reserve ((size_t) maxPendingEvents);
    seedCounter = std::random_device{}();
    performanceSeed = mix64 (seedCounter);
}

void HumanizerEngine::prepare (double newSampleRate, int lookaheadSamples)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    lookahead = std::max (0, lookaheadSamples);
    pending.reserve ((size_t) maxPendingEvents);
    reset();
}

void HumanizerEngine::reset()
{
    pending.clear();
    keys.fill ({});
    blockStart = 0;
    blockLength = 0;
    lastBlockUsedPpq = false;
    nextOrder = 0;
}

void HumanizerEngine::setLookaheadSamples (int lookaheadSamples) noexcept
{
    lookahead = std::max (0, lookaheadSamples);
}

uint64_t HumanizerEngine::currentSeed() const noexcept
{
    return settings.repeatable ? (uint64_t) settings.seed : performanceSeed;
}

void HumanizerEngine::beginBlock (int numSamples, const Transport& newTransport) noexcept
{
    blockLength = std::max (0, numSamples);
    transport = newTransport;

    if (transport.bpm <= 0.0)
        transport.bpm = 120.0;

    const bool usePpq = transport.isPlaying && transport.hasPpq;

    if (usePpq)
    {
        const bool started = ! lastBlockUsedPpq;
        const bool jumped = lastBlockUsedPpq && std::abs (transport.ppqAtBlockStart - expectedPpq) > 0.05;

        if (started || jumped)
        {
            // Hits before a jump / loop are unrelated to the ones after it.
            for (auto& key : keys)
                key.gapValid = false;
        }

        if (started)
            performanceSeed = mix64 (++seedCounter + golden); // a new take for non-repeatable mode

        expectedPpq = transport.ppqAtBlockStart + blockLength * transport.bpm / (60.0 * sampleRate);
    }

    lastBlockUsedPpq = usePpq;
}

void HumanizerEngine::endBlock() noexcept
{
    blockStart += blockLength;
}

bool HumanizerEngine::pushEvent (const MidiEvent& event, HitInfo* hit) noexcept
{
    if (event.size == 0)
        return false;

    const auto inputTime = blockStart + std::clamp (event.sampleOffset, 0, std::max (0, blockLength - 1));

    if (isNoteOn (event))
        return scheduleNoteOn (event, inputTime, hit);

    if (isNoteOff (event))
    {
        scheduleNoteOff (event, inputTime);
        return false;
    }

    auto time = inputTime + lookahead;

    // All-notes-off / all-sound-off must not overtake notes that are still waiting.
    const auto status = event.data[0] & 0xF0;
    if (event.size >= 3 && status == 0xB0 && (event.data[1] == 120 || event.data[1] == 123))
        time = std::max (time, latestPendingNoteTime (event.data[0] & 0x0F, time - 1) + 1);

    insert (time, otherPriority, event);
    return false;
}

bool HumanizerEngine::scheduleNoteOn (const MidiEvent& event, int64_t inputTime, HitInfo* hit) noexcept
{
    const int channel = event.data[0] & 0x0F;
    const int note = event.data[1] & 0x7F;
    const int velocityIn = event.data[2] & 0x7F;
    const auto group = groupForNote (note);
    const auto& groupSettings = settings.groups[(size_t) group];

    const bool usePpq = transport.isPlaying && transport.hasPpq;
    const double ppq = usePpq ? transport.ppqAtBlockStart + (double) (inputTime - blockStart) * transport.bpm / (60.0 * sampleRate)
                              : 0.0;
    const uint64_t seed = currentSeed();

    // With a running transport, the random values only depend on the musical position of
    // the note, so every playback produces the same performance.
    uint64_t key = combine (seed, saltNote);
    key = usePpq ? combine (key, (uint64_t) std::llround (ppq * ticksPerQuarter))
                 : combine (combine (key, saltLive), liveCounter++);
    key = combine (combine (key, (uint64_t) channel), (uint64_t) note);
    Rng rng (key);

    const double timingRandom = rng.bell();
    const double velocityRandom = rng.bell();
    const double amount = std::clamp ((double) settings.amount, 0.0, 1.0);

    //--- timing
    const double beats = usePpq ? ppq : (double) inputTime / sampleRate * 2.0;
    double jitterMs = amount * (settings.timingMs * groupSettings.timingScale * timingRandom
                                + settings.driftMs * driftAt (seed, beats));

    if (lookahead <= 0)
        jitterMs = std::abs (jitterMs); // no lookahead: hits can only be late

    const double lookaheadMs = lookahead * 1000.0 / sampleRate;
    const double offsetMs = std::clamp (settings.feelMs + groupSettings.shiftMs + jitterMs, -lookaheadMs, maxLateMs);

    auto outputTime = std::max (inputTime, inputTime + lookahead + (int64_t) std::llround (offsetMs * sampleRate / 1000.0));

    auto& state = keyState (channel, note);

    if (state.hasNoteOn)
    {
        // Never swap two hits on the same drum, and keep at least half of their original spacing.
        const auto minimumGap = state.gapValid ? std::max<int64_t> (1, (inputTime - state.lastOnIn) / 2) : 1;
        outputTime = std::max (outputTime, state.lastOnOut + minimumGap);
    }

    // A note-off of the previous hit on this drum must not cut the new one.
    moveNoteOffsTo (channel, note, outputTime);

    state.hasNoteOn = true;
    state.gapValid = true;
    state.lastOnIn = inputTime;
    state.lastOnOut = outputTime;
    state.lastDelay = outputTime - inputTime;

    //--- velocity
    const double accentDelta = usePpq ? accentFor (group, classifyGridPosition (ppq, transport)) * settings.accent : 0.0;
    const double delta = amount * (accentDelta + settings.velocityRange * groupSettings.velocityScale * velocityRandom);

    // Leave some headroom when the input is already (close to) full velocity,
    // otherwise all the louder hits would be clipped to 127.
    const double base = velocityIn - amount * std::max (0.0, minVelocityHeadroom - (127.0 - velocityIn));
    const double limited = delta >= 0.0 ? softLimit (delta, 127.0 - base) : -softLimit (-delta, base - 1.0);
    const int velocityOut = std::clamp ((int) std::lround (base + limited), 1, 127);

    auto out = event;
    out.data[2] = (uint8_t) velocityOut;
    insert (outputTime, noteOnPriority, out);

    if (hit != nullptr)
    {
        hit->note = note;
        hit->channel = channel;
        hit->velocityIn = velocityIn;
        hit->velocityOut = velocityOut;
        hit->offsetMs = (float) ((double) (outputTime - inputTime - lookahead) * 1000.0 / sampleRate);
        hit->group = group;
    }

    return true;
}

void HumanizerEngine::scheduleNoteOff (const MidiEvent& event, int64_t inputTime) noexcept
{
    const auto& state = keyState (event.data[0] & 0x0F, event.data[1] & 0x7F);

    // Keep the note's duration, but never end it before it started.
    const auto time = state.hasNoteOn ? std::max (inputTime + state.lastDelay, state.lastOnOut + 1)
                                      : inputTime + lookahead;

    insert (time, noteOffPriority, event);
}

bool HumanizerEngine::comesBefore (const PendingEvent& a, const PendingEvent& b) noexcept
{
    if (a.time != b.time)         return a.time < b.time;
    if (a.priority != b.priority) return a.priority < b.priority;
    return a.order < b.order;
}

void HumanizerEngine::insert (int64_t time, int priority, const MidiEvent& event) noexcept
{
    if (pending.size() >= (size_t) maxPendingEvents)
        return; // cannot happen with musical input (thousands of events within ~0.1 s)

    const PendingEvent item { time, priority, nextOrder++, event };

    const auto position = std::upper_bound (pending.begin(), pending.end(), item, comesBefore);

    pending.insert (position, item);
}

void HumanizerEngine::moveNoteOffsTo (int channel, int note, int64_t time) noexcept
{
    bool moved = false;

    for (auto& item : pending)
    {
        if (item.time > time && isNoteOff (item.event)
            && (item.event.data[0] & 0x0F) == channel && (item.event.data[1] & 0x7F) == note)
        {
            item.time = time;
            moved = true;
        }
    }

    if (moved)
        std::sort (pending.begin(), pending.end(), comesBefore);
}

int64_t HumanizerEngine::latestPendingNoteTime (int channel, int64_t fallback) const noexcept
{
    auto latest = fallback;

    for (const auto& item : pending)
        if ((isNoteOn (item.event) || isNoteOff (item.event)) && (item.event.data[0] & 0x0F) == channel)
            latest = std::max (latest, item.time);

    return latest;
}

double HumanizerEngine::driftAt (uint64_t seed, double beats) const noexcept
{
    // Smooth value noise: a random point every couple of beats, eased in between.
    const double x = beats / driftPeriodBeats;
    const double index = std::floor (x);
    const double fraction = x - index;

    const auto valueAt = [seed] (int64_t i)
    {
        Rng rng (combine (combine (seed, saltDrift), (uint64_t) i));
        return rng.uniform() * 2.0 - 1.0;
    };

    const double a = valueAt ((int64_t) index);
    const double b = valueAt ((int64_t) index + 1);
    const double eased = fraction * fraction * (3.0 - 2.0 * fraction);
    return a + (b - a) * eased;
}

} // namespace dh
