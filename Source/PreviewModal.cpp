#include "PreviewModal.h"
#include <thread>

PreviewModal::PreviewModal(PluginManager& manager)
    : pluginManager(manager)
{
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    auto configureLabel = [this](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    };

    configureLabel(totalEventsLabel);
    configureLabel(uniquePluginsLabel);
    configureLabel(durationLabel);
    configureLabel(noteOnLabel);
    configureLabel(noteOffLabel);
    configureLabel(ccLabel);
    configureLabel(otherLabel);
    configureLabel(transportLabel);
    configureLabel(renderInfoLabel);

    playButton.onClick = [this]()
    {
        pluginManager.previewPlay();
        refreshSummaryAndState();
    };
    pauseButton.onClick = [this]()
    {
        pluginManager.previewPause();
        refreshSummaryAndState();
    };
    stopButton.onClick = [this]()
    {
        pluginManager.previewStop();
        refreshSummaryAndState();
    };
    closeButton.onClick = [this]()
    {
        pluginManager.previewStop();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    renderButton.onClick = [this]()
    {
        handleRenderRequest();
    };
    openFolderButton.onClick = [this]()
    {
        if (lastRenderFolder.exists())
        {
            lastRenderFolder.startAsProcess();
        }
    };

    addAndMakeVisible(playButton);
    addAndMakeVisible(pauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(closeButton);
    addAndMakeVisible(renderButton);
    addAndMakeVisible(openFolderButton);

    refreshSummaryAndState();
    startTimerHz(5);
}

void PreviewModal::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    titleLabel.setBounds(bounds.removeFromTop(30));

    auto lineHeight = 24;
    totalEventsLabel.setBounds(bounds.removeFromTop(lineHeight));
    uniquePluginsLabel.setBounds(bounds.removeFromTop(lineHeight));
    durationLabel.setBounds(bounds.removeFromTop(lineHeight));
    noteOnLabel.setBounds(bounds.removeFromTop(lineHeight));
    noteOffLabel.setBounds(bounds.removeFromTop(lineHeight));
    ccLabel.setBounds(bounds.removeFromTop(lineHeight));
    otherLabel.setBounds(bounds.removeFromTop(lineHeight));
    transportLabel.setBounds(bounds.removeFromTop(lineHeight));

    bounds.removeFromTop(10);
    auto buttonRow = bounds.removeFromTop(30);
    playButton.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(6);
    pauseButton.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(6);
    stopButton.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(6);
    renderButton.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(6);
    openFolderButton.setBounds(buttonRow.removeFromLeft(100));

    bounds.removeFromTop(6);
    closeButton.setBounds(bounds.removeFromTop(28).removeFromLeft(100));

    bounds.removeFromTop(8);
    renderInfoLabel.setBounds(bounds.removeFromTop(40));
}

void PreviewModal::timerCallback()
{
    refreshSummaryAndState();
}

void PreviewModal::refreshSummaryAndState()
{
    const auto summary = pluginManager.getMasterTaggedMidiSummary();
    totalEventsLabel.setText("Total Events: " + juce::String((int)summary.totalEvents), juce::dontSendNotification);
    uniquePluginsLabel.setText("Unique Plugins: " + juce::String(summary.uniquePluginCount), juce::dontSendNotification);
    const double durationSeconds = summary.durationMs / 1000.0;
    durationLabel.setText("Duration: " + juce::String(summary.durationMs, 2) + " ms (" + juce::String(durationSeconds, 2) + " s)", juce::dontSendNotification);
    noteOnLabel.setText("Note On: " + juce::String(summary.noteOnCount), juce::dontSendNotification);
    noteOffLabel.setText("Note Off: " + juce::String(summary.noteOffCount), juce::dontSendNotification);
    ccLabel.setText("CC: " + juce::String(summary.ccCount), juce::dontSendNotification);
    otherLabel.setText("Other: " + juce::String(summary.otherCount), juce::dontSendNotification);

    const bool active = pluginManager.isPreviewActive();
    const bool paused = pluginManager.isPreviewPaused();
    juce::String stateText = "State: Stopped";
    if (active && paused)
        stateText = "State: Paused";
    else if (active)
        stateText = "State: Playing";
    transportLabel.setText(stateText, juce::dontSendNotification);

    if (renderJobRunning.load())
    {
        const float progress = pluginManager.getRenderProgress();
        juce::String progressText = "Rendering... " + juce::String(progress * 100.0f, 1) + "%";
        renderInfoLabel.setText(progressText, juce::dontSendNotification);
    }

    const bool hasEvents = summary.totalEvents > 0;
    playButton.setEnabled(hasEvents && (!active || paused));
    pauseButton.setEnabled(active && !paused);
    stopButton.setEnabled(active || paused);
    renderButton.setEnabled(hasEvents && !renderJobRunning.load());
    openFolderButton.setEnabled(lastRenderFolder.exists());
}

void PreviewModal::handleRenderRequest()
{
    if (!pluginManager.hasMasterTaggedMidiData())
        return;

    juce::File defaultDir = lastRenderFolder;
    if (!defaultDir.exists())
        defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    juce::FileChooser chooser("Choose render output folder", defaultDir, "*", true);
    if (!chooser.browseForDirectory())
        return;

    lastRenderFolder = chooser.getResult();

    const int blockSize = pluginManager.getCurrentBlockSize();
    constexpr double tailSeconds = 2.0;
    launchRenderJob(lastRenderFolder, blockSize > 0 ? blockSize : 512, tailSeconds);
}

void PreviewModal::launchRenderJob(const juce::File& folder, int blockSize, double tailSeconds)
{
    if (renderJobRunning.load())
        return;

    double sampleRate = pm.getCurrentSampleRate();
    if (sampleRate <= 0.0)
    {
        if (auto* device = pm.getAudioDeviceManager().getCurrentAudioDevice())
            sampleRate = device->getCurrentSampleRate();
    }
    if (sampleRate <= 0.0)
    {
        renderInfoLabel.setText("Render failed: invalid sample rate.", juce::dontSendNotification);
        return;
    }

    renderJobRunning.store(true);
    renderInfoLabel.setText("Render starting...", juce::dontSendNotification);

    auto safeThis = juce::Component::SafePointer<PreviewModal>(this);
    PluginManager& pm = pluginManager;
    const double sampleRate = pm.getCurrentSampleRate();
    pm.beginExclusiveRender(sampleRate, blockSize);

    std::thread([safeThis, &pm, folder, blockSize, tailSeconds]()
    {
        const bool ok = pm.renderMaster(folder, "Capture", blockSize, tailSeconds);
        pm.endExclusiveRender();
        juce::MessageManager::callAsync([safeThis, ok, folder]()
        {
            if (auto* self = safeThis.getComponent())
            {
                self->renderJobRunning.store(false);
                self->lastRenderFolder = folder;
                if (ok)
                    self->renderInfoLabel.setText("Render complete. Master.wav saved to " + folder.getFullPathName(),
                        juce::dontSendNotification);
                else
                    self->renderInfoLabel.setText("Render failed. See logs for details.",
                        juce::dontSendNotification);
                self->openFolderButton.setEnabled(folder.exists());
            }
        });
    }).detach();
}

PreviewModal::~PreviewModal() = default;
