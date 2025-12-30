#pragma once

#include <JuceHeader.h>
#include "PluginManager.h"
#include <atomic>

class PreviewModal : public juce::Component,
                     private juce::Timer
{
public:
    explicit PreviewModal(PluginManager& manager);
    ~PreviewModal() override;

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

    juce::TextButton playButton{ "Play capture" };
    juce::TextButton pauseButton{ "Pause" };
    juce::TextButton stopButton{ "Stop" };
    juce::TextButton closeButton{ "Close" };
    juce::TextButton saveCaptureButton{ "Save Capture" };
    juce::TextButton loadCaptureButton{ "Load Capture" };
    juce::TextButton renderButton{ "Render" };
    juce::TextButton openFolderButton{ "Open Folder" };

    juce::File lastRenderFolder;
    juce::File lastCaptureFile;
    std::atomic<bool> renderJobRunning{ false };

    void handleRenderRequest();
    void launchRenderJob(const juce::File& folder, int blockSize, double tailSeconds, juce::String projectName);
};
