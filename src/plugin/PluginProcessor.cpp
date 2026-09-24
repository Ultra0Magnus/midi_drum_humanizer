#include "PluginProcessor.h"
#include "PluginEditor.h"

MidiDrumHumanizerProcessor::MidiDrumHumanizerProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "MidiDrumHumanizer", params::createLayout()),
      settingsReader (state)
{
}

void MidiDrumHumanizerProcessor::prepareToPlay (double sampleRate, int)
{
    maxLookaheadSamples = (int) std::lround (lookaheadMs * sampleRate / 1000.0);
    lookaheadActive = settingsReader.lookaheadEnabled();

    const int lookahead = lookaheadActive ? maxLookaheadSamples : 0;
    engine.prepare (sampleRate, lookahead);
    setLatencySamples (lookahead);

    outputMidi.ensureSize (8192);

    audioDelayLine.setSize (juce::jmax (1, getTotalNumOutputChannels()), maxLookaheadSamples + 1);
    audioDelayLine.clear();
    audioDelayLength = lookahead;
    audioDelayPosition = 0;
}

void MidiDrumHumanizerProcessor::releaseResources() {}

bool MidiDrumHumanizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    const auto& input = layouts.getMainInputChannelSet();
    return input == output || input.isDisabled();
}

//==============================================================================
void MidiDrumHumanizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    updateLookahead();
    process (buffer, midi, settingsReader.read(), true);
}

void MidiDrumHumanizerProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Bypassed: no humanization, but keep the latency so everything stays in sync.
    updateLookahead();

    auto settings = settingsReader.read();
    settings.amount = 0.0f;
    settings.feelMs = 0.0f;
    for (auto& group : settings.groups)
        group.shiftMs = 0.0f;

    process (buffer, midi, settings, false);
}

void MidiDrumHumanizerProcessor::updateLookahead()
{
    const bool enabled = settingsReader.lookaheadEnabled();

    if (enabled == lookaheadActive)
        return;

    lookaheadActive = enabled;
    const int lookahead = enabled ? maxLookaheadSamples : 0;

    engine.setLookaheadSamples (lookahead);
    audioDelayLine.clear();
    audioDelayLength = lookahead;
    audioDelayPosition = 0;
    setLatencySamples (lookahead);
}

void MidiDrumHumanizerProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                          const dh::Settings& settings, bool reportHits)
{
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    delayAudio (buffer);

    engine.setSettings (settings);
    engine.beginBlock (buffer.getNumSamples(), readTransport());
    outputMidi.clear();

    for (const auto metadata : midi)
    {
        if (metadata.numBytes > 3)
        {
            outputMidi.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition); // SysEx: passed on as is
            continue;
        }

        dh::MidiEvent event;
        event.sampleOffset = metadata.samplePosition;
        event.size = (uint8_t) metadata.numBytes;
        std::copy_n (metadata.data, metadata.numBytes, event.data);

        dh::HitInfo hit;
        if (engine.pushEvent (event, &hit) && reportHits)
            pushHit (hit);
    }

    engine.popDueEvents ([this] (const dh::MidiEvent& event)
    {
        outputMidi.addEvent (event.data, event.size, event.sampleOffset);
    });

    engine.endBlock();
    midi.swapWith (outputMidi);
}

dh::Transport MidiDrumHumanizerProcessor::readTransport() const
{
    dh::Transport transport;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            transport.isPlaying = position->getIsPlaying();

            if (const auto ppq = position->getPpqPosition())
            {
                transport.hasPpq = true;
                transport.ppqAtBlockStart = *ppq;
            }

            if (const auto bpm = position->getBpm())
                transport.bpm = *bpm;

            if (const auto barStart = position->getPpqPositionOfLastBarStart())
            {
                transport.hasBarStart = true;
                transport.ppqOfLastBarStart = *barStart;
            }

            if (const auto signature = position->getTimeSignature())
            {
                transport.timeSigNumerator = signature->numerator;
                transport.timeSigDenominator = signature->denominator;
            }
        }
    }

    return transport;
}

void MidiDrumHumanizerProcessor::delayAudio (juce::AudioBuffer<float>& buffer)
{
    if (audioDelayLength <= 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), audioDelayLine.getNumChannels());
    int position = audioDelayPosition;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        auto* line = audioDelayLine.getWritePointer (channel);
        position = audioDelayPosition;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto input = samples[i];
            samples[i] = line[position];
            line[position] = input;

            if (++position >= audioDelayLength)
                position = 0;
        }
    }

    audioDelayPosition = position;
}

void MidiDrumHumanizerProcessor::pushHit (const dh::HitInfo& hit)
{
    const auto scope = hitFifo.write (1);

    if (scope.blockSize1 > 0)
        hitBuffer[(size_t) scope.startIndex1] = hit;
    else if (scope.blockSize2 > 0)
        hitBuffer[(size_t) scope.startIndex2] = hit;
}

//==============================================================================
juce::AudioProcessorEditor* MidiDrumHumanizerProcessor::createEditor()
{
    return new MidiDrumHumanizerEditor (*this);
}

void MidiDrumHumanizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MidiDrumHumanizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiDrumHumanizerProcessor();
}
