#pragma once

#include <JuceHeader.h>
#include "PluginManager.h"

struct RenderEvent
{
    juce::String pluginId;
    juce::MidiMessage message;
    juce::int64 samplePos = 0;
};

std::vector<RenderEvent> buildRenderTimelineFromSnapshot(
    const std::vector<MyMidiMessage>& snapshot,
    double renderZeroMs,
    double sampleRate);

juce::int64 computeEndSampleWithTail(const std::vector<RenderEvent>& events,
    double sampleRate,
    double tailSeconds);
