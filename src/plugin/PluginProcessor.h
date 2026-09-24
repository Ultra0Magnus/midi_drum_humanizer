#pragma once

#include "HumanizerEngine.h"
#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

class MidiDrumHumanizerProcessor final : public juce::AudioProcessor
{
public:
    // Fixed lookahead that lets hits land before the beat. Reported to the host as latency.
    static constexpr double lookaheadMs = 40.0;

    MidiDrumHumanizerProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return (lookaheadMs + dh::HumanizerEngine::maxLateMs) / 1000.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    // Hits played since the last call, for the editor's hit monitor (message thread).
    template <typename Callback>
    void readHits (Callback&& callback)
    {
        const auto scope = hitFifo.read (hitFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) callback (hitBuffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) callback (hitBuffer[(size_t) (scope.startIndex2 + i)]);
    }

private:
    void updateLookahead();
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, const dh::Settings&, bool reportHits);
    dh::Transport readTransport() const;
    void delayAudio (juce::AudioBuffer<float>&);
    void pushHit (const dh::HitInfo&);

    juce::AudioProcessorValueTreeState state;
    params::SettingsReader settingsReader;

    dh::HumanizerEngine engine;
    juce::MidiBuffer outputMidi;
    bool lookaheadActive = true;
    int maxLookaheadSamples = 0;

    // Audio passing through is delayed like the MIDI, so the reported latency is correct.
    juce::AudioBuffer<float> audioDelayLine;
    int audioDelayLength = 0;
    int audioDelayPosition = 0;

    static constexpr int hitFifoSize = 512;
    juce::AbstractFifo hitFifo { hitFifoSize };
    std::array<dh::HitInfo, hitFifoSize> hitBuffer {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDrumHumanizerProcessor)
};
