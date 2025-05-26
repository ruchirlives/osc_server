/*
  ==============================================================================

    HostPlayHead.h
    Created: 30 Apr 2025 7:37:37pm
    Author:  Ruchirlives

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

struct HostPlayHead : public juce::AudioPlayHead
{
    PositionInfo positionInfo;

    // JUCE 6+ / JUCE 8 callback must be const
    juce::Optional<PositionInfo> getPosition() const override
    {
        return positionInfo;
    }
};

// declare the global — every .cpp that includes this sees the name
extern HostPlayHead hostPlayHead;
