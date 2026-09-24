// Unit tests for the humanizer engine. Run with: ctest --test-dir build --output-on-failure

#include "HumanizerEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <tuple>
#include <vector>

namespace
{
//==============================================================================
struct TestCase
{
    const char* name;
    void (*function)();
};

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar (const char* name, void (*function)()) { registry().push_back ({ name, function }); }
};

int failures = 0;

#define TEST(name)                                          \
    static void name();                                     \
    static const Registrar name##Registrar (#name, name);   \
    static void name()

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            std::printf ("    FAILED: %s  (%s:%d)\n", #condition, __FILE__, __LINE__);     \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

//==============================================================================
struct Event
{
    int64_t time;
    uint8_t status, data1, data2;

    bool isNoteOn() const { return (status & 0xF0) == 0x90 && data2 > 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
    int key() const { return (status & 0x0F) * 128 + data1; }

    bool operator< (const Event& o) const { return std::tie (time, status, data1, data2) < std::tie (o.time, o.status, o.data1, o.data2); }
    bool operator== (const Event& o) const { return std::tie (time, status, data1, data2) == std::tie (o.time, o.status, o.data1, o.data2); }
};

Event noteOn (int64_t t, int note, int velocity = 100, int channel = 9) { return { t, (uint8_t) (0x90 | channel), (uint8_t) note, (uint8_t) velocity }; }
Event noteOff (int64_t t, int note, int channel = 9) { return { t, (uint8_t) (0x80 | channel), (uint8_t) note, 0 }; }

// Simulates a host calling the plugin block by block, with a running (or stopped) transport.
struct Host
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    int blockSize = 512;
    bool playing = true;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    // Absolute sample clock of the engine; keeps running across calls to run().
    int64_t clock = 0;

    int64_t samplesPerBeat() const { return (int64_t) std::llround (sampleRate * 60.0 / bpm); }

    // Runs `length` samples of transport starting at musical position startPpq.
    // Input times are relative to the start of this run; the returned output times too.
    std::vector<Event> run (dh::HumanizerEngine& engine, std::vector<Event> input, int64_t length,
                            double startPpq = 0.0, std::vector<dh::HitInfo>* hits = nullptr)
    {
        std::stable_sort (input.begin(), input.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });

        std::vector<Event> output;
        size_t next = 0;
        const int64_t runStart = clock;

        for (int64_t pos = 0; pos < length; pos += blockSize)
        {
            const int n = (int) std::min<int64_t> (blockSize, length - pos);

            dh::Transport transport;
            transport.isPlaying = playing;
            transport.hasPpq = true;
            transport.bpm = bpm;
            transport.ppqAtBlockStart = startPpq + (double) pos * bpm / (60.0 * sampleRate);
            const double barLength = timeSigNumerator * 4.0 / timeSigDenominator;
            transport.hasBarStart = true;
            transport.ppqOfLastBarStart = std::floor (transport.ppqAtBlockStart / barLength) * barLength;
            transport.timeSigNumerator = timeSigNumerator;
            transport.timeSigDenominator = timeSigDenominator;

            engine.beginBlock (n, transport);

            for (; next < input.size() && input[next].time < pos + n; ++next)
            {
                dh::MidiEvent e;
                e.sampleOffset = (int) (input[next].time - pos);
                e.data[0] = input[next].status;
                e.data[1] = input[next].data1;
                e.data[2] = input[next].data2;
                e.size = 3;

                dh::HitInfo hit;
                if (engine.pushEvent (e, &hit) && hits != nullptr)
                    hits->push_back (hit);
            }

            engine.popDueEvents ([&] (const dh::MidiEvent& e)
            {
                output.push_back ({ clock - runStart + e.sampleOffset, e.data[0], e.data[1], e.data[2] });
            });

            engine.endBlock();
            clock += n;
        }

        return output;
    }
};

// A completely robotic 4/4 rock beat: kick on 1 and 3, snare on 2 and 4, 8th-note hats,
// every note at the same velocity and exactly on the grid.
std::vector<Event> robotBeat (const Host& host, int bars, int velocity = 100)
{
    std::vector<Event> events;
    const auto beat = host.samplesPerBeat();
    const auto length = beat / 8;

    for (int b = 0; b < bars * 4; ++b)
    {
        const auto t = b * beat;
        const int drum = (b % 2 == 0) ? 36 : 38;
        events.push_back (noteOn (t, drum, velocity));
        events.push_back (noteOff (t + length, drum));

        for (int h = 0; h < 2; ++h)
        {
            events.push_back (noteOn (t + h * beat / 2, 42, velocity));
            events.push_back (noteOff (t + h * beat / 2 + length, 42));
        }
    }

    return events;
}

dh::Settings defaultSettings()
{
    dh::Settings s;
    s.groups[dh::Kick]     = { 0.8f, 0.7f, 0.0f };
    s.groups[dh::Snare]    = { 0.7f, 0.7f, 0.0f };
    s.groups[dh::HatsRide] = { 1.2f, 1.0f, 0.0f };
    s.groups[dh::Cymbals]  = { 0.8f, 1.0f, 0.0f };
    return s;
}

dh::Settings neutralSettings()
{
    dh::Settings s;
    s.amount = 0.0f;
    s.feelMs = 0.0f;
    return s;
}

constexpr int lookahead = 1920; // 40 ms at 48 kHz

std::vector<Event> filter (const std::vector<Event>& events, bool (Event::*predicate)() const)
{
    std::vector<Event> result;
    for (const auto& e : events)
        if ((e.*predicate)())
            result.push_back (e);
    return result;
}

// Pairs the n-th note-on of every key in `input` with the n-th note-on of that key in `output`.
std::vector<std::pair<Event, Event>> pairNoteOns (const std::vector<Event>& input, const std::vector<Event>& output)
{
    std::map<int, std::vector<Event>> in, out;
    for (const auto& e : input)  if (e.isNoteOn()) in[e.key()].push_back (e);
    for (const auto& e : output) if (e.isNoteOn()) out[e.key()].push_back (e);

    std::vector<std::pair<Event, Event>> pairs;
    for (auto& [key, list] : in)
    {
        std::stable_sort (list.begin(), list.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });
        const auto& outs = out[key];
        for (size_t i = 0; i < std::min (list.size(), outs.size()); ++i)
            pairs.emplace_back (list[i], outs[i]);
    }
    return pairs;
}

// Every key must alternate note-on / note-off, starting with an on and ending with an off.
bool notesArePairedCorrectly (const std::vector<Event>& output)
{
    std::map<int, bool> sounding;

    for (const auto& e : output)
    {
        if (e.isNoteOn())
        {
            if (sounding[e.key()])
                return false;
            sounding[e.key()] = true;
        }
        else if (e.isNoteOff())
        {
            if (! sounding[e.key()])
                return false;
            sounding[e.key()] = false;
        }
    }

    for (const auto& [key, on] : sounding)
        if (on)
            return false;

    return true;
}

//==============================================================================
TEST (drumMapFollowsGeneralMidi)
{
    CHECK (dh::groupForNote (36) == dh::Kick);
    CHECK (dh::groupForNote (35) == dh::Kick);
    CHECK (dh::groupForNote (38) == dh::Snare);
    CHECK (dh::groupForNote (40) == dh::Snare);
    CHECK (dh::groupForNote (42) == dh::HatsRide);
    CHECK (dh::groupForNote (46) == dh::HatsRide);
    CHECK (dh::groupForNote (51) == dh::HatsRide);
    CHECK (dh::groupForNote (45) == dh::Toms);
    CHECK (dh::groupForNote (49) == dh::Cymbals);
    CHECK (dh::groupForNote (39) == dh::Other);
    CHECK (dh::groupForNote (100) == dh::Other);
}

TEST (gridPositionsAreClassified)
{
    dh::Transport t;
    t.hasBarStart = true;
    t.ppqOfLastBarStart = 0.0;

    CHECK (dh::classifyGridPosition (0.0, t) == dh::GridPosition::Downbeat);
    CHECK (dh::classifyGridPosition (1.0, t) == dh::GridPosition::Beat);
    CHECK (dh::classifyGridPosition (3.0, t) == dh::GridPosition::Beat);
    CHECK (dh::classifyGridPosition (0.5, t) == dh::GridPosition::Eighth);
    CHECK (dh::classifyGridPosition (2.25, t) == dh::GridPosition::Sixteenth);
    CHECK (dh::classifyGridPosition (2.75, t) == dh::GridPosition::Sixteenth);
    CHECK (dh::classifyGridPosition (1.0 / 3.0, t) == dh::GridPosition::Triplet);
    CHECK (dh::classifyGridPosition (0.125, t) == dh::GridPosition::ThirtySecond);
    CHECK (dh::classifyGridPosition (0.19, t) == dh::GridPosition::OffGrid);
    CHECK (dh::classifyGridPosition (3.995, t) == dh::GridPosition::Downbeat); // slightly early next bar
    CHECK (dh::classifyGridPosition (4.0, t) == dh::GridPosition::Downbeat);   // block crossing a barline
    CHECK (dh::classifyGridPosition (1.01, t) == dh::GridPosition::Beat);      // slightly unquantized

    // 6/8: two dotted-quarter beats per bar
    t.timeSigNumerator = 6;
    t.timeSigDenominator = 8;
    CHECK (dh::classifyGridPosition (0.0, t) == dh::GridPosition::Downbeat);
    CHECK (dh::classifyGridPosition (1.5, t) == dh::GridPosition::Beat);
    CHECK (dh::classifyGridPosition (0.5, t) == dh::GridPosition::Eighth);
    CHECK (dh::classifyGridPosition (0.25, t) == dh::GridPosition::Sixteenth);
    CHECK (dh::classifyGridPosition (3.0, t) == dh::GridPosition::Downbeat);
}

TEST (zeroAmountIsAPureDelay)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);
    engine.setSettings (neutralSettings());

    const auto input = robotBeat (host, 2);
    auto output = host.run (engine, input, 3 * 4 * host.samplesPerBeat());

    auto expected = input;
    for (auto& e : expected)
        e.time += lookahead;

    std::sort (output.begin(), output.end());
    std::sort (expected.begin(), expected.end());
    CHECK (output == expected);
}

TEST (humanizesVelocityAndTiming)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);
    engine.setSettings (defaultSettings());

    const auto input = robotBeat (host, 8);
    const auto output = host.run (engine, input, 9 * 4 * host.samplesPerBeat());
    const auto pairs = pairNoteOns (input, output);

    CHECK (pairs.size() == filter (input, &Event::isNoteOn).size());
    CHECK (filter (output, &Event::isNoteOn).size() == filter (input, &Event::isNoteOn).size());
    CHECK (filter (output, &Event::isNoteOff).size() == filter (input, &Event::isNoteOff).size());

    std::set<int> velocities, offsets;
    for (const auto& [in, out] : pairs)
    {
        velocities.insert (out.data2);
        offsets.insert ((int) (out.time - in.time));
    }

    CHECK (velocities.size() > 15);
    CHECK (offsets.size() > 30);
    CHECK (notesArePairedCorrectly (output));
}

TEST (outputStaysInsideTheTimingWindow)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);

    auto s = defaultSettings();
    s.timingMs = 30.0f;
    s.driftMs = 15.0f;
    s.feelMs = -25.0f;
    engine.setSettings (s);

    const auto input = robotBeat (host, 8);
    const auto output = host.run (engine, input, 9 * 4 * host.samplesPerBeat());

    const auto maxLate = (int64_t) (dh::HumanizerEngine::maxLateMs * host.sampleRate / 1000.0);
    bool anyEarly = false;

    for (const auto& [in, out] : pairNoteOns (input, output))
    {
        CHECK (out.time >= in.time);                        // never earlier than the lookahead allows
        CHECK (out.time <= in.time + lookahead + maxLate);
        anyEarly = anyEarly || out.time < in.time + lookahead;
    }

    CHECK (anyEarly);
    CHECK (notesArePairedCorrectly (output));
}

TEST (timingDistributionIsBellShaped)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);

    dh::Settings s;
    s.timingMs = 10.0f;
    s.driftMs = 0.0f;
    engine.setSettings (s);

    const auto input = robotBeat (host, 64);
    const auto output = host.run (engine, input, 65 * 4 * host.samplesPerBeat());

    double sum = 0.0, sumSquares = 0.0, maxAbs = 0.0;
    int count = 0;

    for (const auto& [in, out] : pairNoteOns (input, output))
    {
        const double ms = (double) (out.time - in.time - lookahead) * 1000.0 / host.sampleRate;
        sum += ms;
        sumSquares += ms * ms;
        maxAbs = std::max (maxAbs, std::abs (ms));
        ++count;
    }

    const double mean = sum / count;
    const double deviation = std::sqrt (sumSquares / count - mean * mean);

    std::printf ("    timing: mean %.2f ms, std dev %.2f ms, max %.2f ms over %d hits\n", mean, deviation, maxAbs, count);
    CHECK (std::abs (mean) < 0.5);
    CHECK (deviation > 2.8 && deviation < 3.9); // 10 ms range -> ~3.3 ms standard deviation
    CHECK (maxAbs <= 10.05);
}

TEST (velocitiesStayValidAndVariedAtFullInputVelocity)
{
    for (const int inputVelocity : { 1, 20, 100, 127 })
    {
        Host host;
        dh::HumanizerEngine engine;
        engine.prepare (host.sampleRate, lookahead);

        auto s = defaultSettings();
        s.velocityRange = 40.0f;
        s.accent = 1.0f;
        engine.setSettings (s);

        const auto output = host.run (engine, robotBeat (host, 8, inputVelocity), 9 * 4 * host.samplesPerBeat());

        std::set<int> velocities;
        for (const auto& e : filter (output, &Event::isNoteOn))
        {
            CHECK (e.data2 >= 1 && e.data2 <= 127);
            velocities.insert (e.data2);
        }

        CHECK (velocities.size() >= 5);

        if (inputVelocity == 127)
            CHECK (velocities.count (127) == 0 || velocities.size() > 10); // not everything pinned to the top
    }
}

TEST (fullVelocityInputKeepsDynamicsBelowTheCeiling)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);
    engine.setSettings (defaultSettings());

    const auto output = filter (host.run (engine, robotBeat (host, 16, 127), 17 * 4 * host.samplesPerBeat()), &Event::isNoteOn);

    int atCeiling = 0;
    double sum = 0.0;
    for (const auto& e : output)
    {
        atCeiling += e.data2 == 127 ? 1 : 0;
        sum += e.data2;
    }

    const double fraction = (double) atCeiling / (double) output.size();
    std::printf ("    input 127: %.0f %% of hits at 127, mean velocity %.1f\n", fraction * 100.0, sum / (double) output.size());
    CHECK (fraction < 0.15);                        // not flattened against the ceiling
    CHECK (sum / (double) output.size() > 108.0);   // but still played loud
}

TEST (accentsFollowTheBeat)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);

    dh::Settings s;
    s.timingMs = 0.0f;
    s.driftMs = 0.0f;
    s.velocityRange = 0.0f;
    s.accent = 1.0f;
    engine.setSettings (s);

    // One bar of 16th-note hi-hats.
    std::vector<Event> input;
    const auto sixteenth = host.samplesPerBeat() / 4;
    for (int i = 0; i < 16; ++i)
    {
        input.push_back (noteOn (i * sixteenth, 42, 90));
        input.push_back (noteOff (i * sixteenth + 100, 42));
    }

    const auto output = filter (host.run (engine, input, 2 * 4 * host.samplesPerBeat()), &Event::isNoteOn);
    CHECK (output.size() == 16);

    if (output.size() == 16)
    {
        const int downbeat = output[0].data2, beat = output[4].data2, eighth = output[2].data2, sixteenthVel = output[1].data2;
        std::printf ("    hi-hat velocities: downbeat %d, beat %d, eighth %d, sixteenth %d\n", downbeat, beat, eighth, sixteenthVel);
        CHECK (downbeat > beat);
        CHECK (beat > eighth);
        CHECK (eighth > sixteenthVel);
        CHECK (output[4].data2 == output[8].data2);   // all beats alike without randomness
        CHECK (output[1].data2 == output[15].data2);  // all 16ths alike
    }
}

TEST (sameSeedGivesSamePerformanceWhateverTheBlockSize)
{
    auto render = [] (int blockSize, uint32_t seed)
    {
        Host host;
        host.blockSize = blockSize;
        dh::HumanizerEngine engine;
        engine.prepare (host.sampleRate, lookahead);
        auto s = defaultSettings();
        s.seed = seed;
        engine.setSettings (s);
        return host.run (engine, robotBeat (host, 4), 5 * 4 * host.samplesPerBeat());
    };

    const auto a = render (512, 1);
    const auto b = render (97, 1);
    const auto c = render (512, 2);

    CHECK (a == b);
    CHECK (a != c);
}

TEST (repeatablePlaybackGivesTheSameTake)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);
    engine.setSettings (defaultSettings());

    const auto input = robotBeat (host, 4);
    const auto length = 5 * 4 * host.samplesPerBeat();

    const auto first = host.run (engine, input, length);
    host.playing = false;
    host.run (engine, {}, 12345);
    host.playing = true;
    const auto second = host.run (engine, input, length);

    CHECK (first == second);
}

TEST (nonRepeatableModeGivesADifferentTakeEachPlayback)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);
    auto s = defaultSettings();
    s.repeatable = false;
    engine.setSettings (s);

    const auto input = robotBeat (host, 4);
    const auto length = 5 * 4 * host.samplesPerBeat();

    const auto first = host.run (engine, input, length);
    host.playing = false;
    host.run (engine, {}, 4800);
    host.playing = true;
    const auto second = host.run (engine, input, length);

    CHECK (first != second);
    CHECK (notesArePairedCorrectly (first));
    CHECK (notesArePairedCorrectly (second));
}

TEST (liveInputIsHumanizedWithoutTransport)
{
    Host host;
    host.playing = false;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, 0);
    engine.setSettings (defaultSettings());

    std::vector<Event> input;
    for (int i = 0; i < 20; ++i)
    {
        input.push_back (noteOn (i * 9600, 38, 100));
        input.push_back (noteOff (i * 9600 + 500, 38));
    }

    const auto output = host.run (engine, input, 21 * 9600);
    std::set<int> velocities;
    for (const auto& e : filter (output, &Event::isNoteOn))
        velocities.insert (e.data2);

    CHECK (velocities.size() > 5);
    CHECK (notesArePairedCorrectly (output));
}

TEST (withoutLookaheadHitsAreOnlyLate)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, 0);

    auto s = defaultSettings();
    s.feelMs = -10.0f;
    s.timingMs = 20.0f;
    engine.setSettings (s);

    const auto input = robotBeat (host, 8);
    const auto output = host.run (engine, input, 9 * 4 * host.samplesPerBeat());

    for (const auto& [in, out] : pairNoteOns (input, output))
        CHECK (out.time >= in.time);

    CHECK (notesArePairedCorrectly (output));

    // Without a push, the timing randomness still spreads the hits instead of pinning them to the grid.
    s.feelMs = 0.0f;
    engine.setSettings (s);
    const auto input2 = robotBeat (host, 8);
    const auto output2 = host.run (engine, input2, 9 * 4 * host.samplesPerBeat());
    const auto pairs = pairNoteOns (input2, output2);

    size_t onGrid = 0;
    for (const auto& [in, out] : pairs)
        onGrid += out.time == in.time ? 1 : 0;

    CHECK (onGrid * 10 < pairs.size());
}

TEST (fastRollsNeverSwapOrder)
{
    Host host;
    host.bpm = 160.0;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);

    auto s = defaultSettings();
    s.timingMs = 30.0f;
    s.driftMs = 15.0f;
    s.groups[dh::Snare].timingScale = 2.0f; // up to +-60 ms, more than the 47 ms between hits
    engine.setSettings (s);

    // Two bars of 32nd-note snare roll.
    std::vector<Event> input;
    const auto step = host.samplesPerBeat() / 8;
    for (int i = 0; i < 64; ++i)
    {
        input.push_back (noteOn (i * step, 38, 100));
        input.push_back (noteOff (i * step + step / 2, 38));
    }

    const auto output = host.run (engine, input, 3 * 4 * host.samplesPerBeat());
    const auto ons = filter (output, &Event::isNoteOn);
    CHECK (ons.size() == 64);

    for (size_t i = 1; i < ons.size(); ++i)
        CHECK (ons[i].time - ons[i - 1].time >= step / 2);

    CHECK (notesArePairedCorrectly (output));
}

TEST (zeroLengthAndOverlappingNotesNeverHang)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);

    auto s = defaultSettings();
    s.timingMs = 30.0f;
    s.driftMs = 15.0f;
    engine.setSettings (s);

    // Random dense input: per key, non-overlapping notes with random (also zero) lengths.
    std::mt19937 random (42);
    std::vector<Event> input;
    const int notes[] = { 36, 38, 42, 46, 49 };

    for (const int note : notes)
    {
        int64_t t = 0;
        for (int i = 0; i < 300; ++i)
        {
            t += std::uniform_int_distribution<int> (1, 2000) (random);
            const auto length = std::uniform_int_distribution<int> (0, 1500) (random);
            input.push_back (noteOn (t, note, std::uniform_int_distribution<int> (1, 127) (random)));
            input.push_back (noteOff (t + length, note));
            t += length;
        }
    }

    // Also a velocity-0 note-on used as note-off, and zero-length notes.
    input.push_back (noteOn (700000, 50, 100));
    input.push_back ({ 700000 + 10, 0x99, 50, 0 });

    for (int i = 0; i < 10; ++i)
    {
        input.push_back (noteOn (710000 + i * 3000, 51, 100));
        input.push_back (noteOff (710000 + i * 3000, 51));
    }

    const auto output = host.run (engine, input, 800000);

    CHECK (filter (output, &Event::isNoteOn).size() == filter (input, &Event::isNoteOn).size());
    CHECK (filter (output, &Event::isNoteOff).size() == filter (input, &Event::isNoteOff).size());
    CHECK (notesArePairedCorrectly (output));
    CHECK (engine.getNumPendingEvents() == 0);
}

TEST (controllersAreDelayedByTheLookahead)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);
    engine.setSettings (defaultSettings());

    const std::vector<Event> input = { { 100, 0xB9, 4, 64 }, { 5000, 0xB9, 4, 127 }, { 7000, 0xE9, 0, 64 } };
    const auto output = host.run (engine, input, 20000);

    CHECK (output.size() == input.size());
    for (size_t i = 0; i < std::min (output.size(), input.size()); ++i)
        CHECK (output[i].time == input[i].time + lookahead && output[i].status == input[i].status);
}

TEST (allNotesOffWaitsForPendingNotes)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);

    auto s = neutralSettings();
    s.feelMs = 50.0f; // everything late
    engine.setSettings (s);

    const std::vector<Event> input = { noteOn (1000, 38), noteOff (1100, 38), { 1200, 0xB9, 123, 0 } };
    const auto output = host.run (engine, input, 20000);

    CHECK (output.size() == 3);
    if (output.size() == 3)
    {
        CHECK (output[0].isNoteOn());
        CHECK (output[1].isNoteOff());
        CHECK (output[2].status == 0xB9 && output[2].data1 == 123);
    }
}

TEST (hitReportsDescribeTheChanges)
{
    Host host;
    dh::HumanizerEngine engine;
    engine.prepare (host.sampleRate, lookahead);
    engine.setSettings (defaultSettings());

    std::vector<dh::HitInfo> hits;
    const auto input = robotBeat (host, 2);
    const auto output = host.run (engine, input, 3 * 4 * host.samplesPerBeat(), 0.0, &hits);

    CHECK (hits.size() == filter (input, &Event::isNoteOn).size());
    for (const auto& hit : hits)
    {
        CHECK (hit.velocityIn == 100);
        CHECK (hit.velocityOut >= 1 && hit.velocityOut <= 127);
        CHECK (std::abs (hit.offsetMs) <= 40.0f);
        CHECK (hit.group == dh::groupForNote (hit.note));
    }
}

} // namespace

int main()
{
    for (const auto& test : registry())
    {
        const int before = failures;
        std::printf ("[ RUN  ] %s\n", test.name);
        test.function();
        std::printf ("[ %s ] %s\n", failures == before ? " OK " : "FAIL", test.name);
    }

    std::printf ("\n%zu tests, %d failed checks\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}
