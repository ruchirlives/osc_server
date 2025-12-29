#include "RenderTimeline.h"
#include <algorithm>

std::vector<RenderEvent> buildRenderTimelineFromSnapshot(
    const std::vector<MyMidiMessage>& snapshot,
    double renderZeroMs,
    double sampleRate)
{
    std::vector<RenderEvent> events;
    events.reserve(snapshot.size());

    if (sampleRate <= 0.0)
        return events;

    for (const auto& entry : snapshot)
    {
        const double deltaMs = static_cast<double>(entry.timestamp) - renderZeroMs;
        const double samples = (deltaMs * sampleRate) / 1000.0;
        RenderEvent evt;
        evt.pluginId = entry.pluginId;
        evt.message = entry.message;
        evt.samplePos = juce::jmax<juce::int64>(0, static_cast<juce::int64>(std::llround(samples)));
        events.push_back(std::move(evt));
    }

    std::sort(events.begin(), events.end(),
        [](const RenderEvent& a, const RenderEvent& b)
        {
            if (a.samplePos == b.samplePos)
                return a.pluginId.compareIgnoreCase(b.pluginId) < 0;
            return a.samplePos < b.samplePos;
        });

    return events;
}

juce::int64 computeEndSampleWithTail(const std::vector<RenderEvent>& events,
    double sampleRate,
    double tailSeconds)
{
    if (events.empty() || sampleRate <= 0.0)
        return 0;

    const auto lastSample = events.back().samplePos;
    const auto tailSamples = static_cast<juce::int64>(std::llround(tailSeconds * sampleRate));
    return lastSample + juce::jmax<juce::int64>(0, tailSamples);
}
