#pragma once

#include <JuceHeader.h>
#include "PluginManager.h"

class PreviewModal : public juce::Component,
                     private juce::Timer
{
public:
    explicit PreviewModal(PluginManager& manager);

    void resized() override;

private:
    void timerCallback() override;
    void refreshSummaryAndState();

    PluginManager& pluginManager;

    juce::Label titleLabel{ "titleLabel", "Preview Capture" };
    juce::Label totalEventsLabel{ "totalEventsLabel", "Total Events: 0" };
    juce::Label uniquePluginsLabel{ "uniquePluginsLabel", "Unique Plugins: 0" };
    juce::Label durationLabel{ "durationLabel", "Duration: 0 ms (0.00 s)" };
    juce::Label noteOnLabel{ "noteOnLabel", "Note On: 0" };
    juce::Label noteOffLabel{ "noteOffLabel", "Note Off: 0" };
    juce::Label ccLabel{ "ccLabel", "CC: 0" };
    juce::Label otherLabel{ "otherLabel", "Other: 0" };
    juce::Label transportLabel{ "transportLabel", "State: Stopped" };
    juce::Label renderInfoLabel{ "renderInfoLabel", "" };

    juce::TextButton playButton{ "Play" };
    juce::TextButton pauseButton{ "Pause" };
    juce::TextButton stopButton{ "Stop" };
    juce::TextButton closeButton{ "Close" };
    juce::TextButton renderButton{ "Render" };

    juce::File lastRenderFolder;

    void handleRenderRequest();
};
