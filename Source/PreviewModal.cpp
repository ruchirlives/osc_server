#include "PreviewModal.h"
#include <thread>

PreviewModal::PreviewModal(PluginManager& manager)
    : pluginManager(manager)
{
    setSize(500, 500);
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
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    renderButton.onClick = [this]()
    {
        handleRenderRequest();
    };
    saveCaptureButton.onClick = [this]()
    {
        if (!pluginManager.hasMasterTaggedMidiData())
            return;

        juce::File defaultDir = lastCaptureFile.exists() ? lastCaptureFile.getParentDirectory()
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OSCDawServer");
        juce::FileChooser chooser("Save Capture", defaultDir, "*.xml", true);
        if (!chooser.browseForFileToSave(true))
            return;

        auto target = chooser.getResult();
        if (!target.hasFileExtension(".xml"))
            target = target.withFileExtension(".xml");

        const bool ok = pluginManager.saveMasterTaggedMidiBufferToFile(target);
        if (ok)
        {
            lastCaptureFile = target;
            renderInfoLabel.setText("Capture saved to " + target.getFullPathName(), juce::dontSendNotification);
        }
        else
        {
            renderInfoLabel.setText("Failed to save capture.", juce::dontSendNotification);
        }
    };
    loadCaptureButton.onClick = [this]()
    {
        juce::File defaultDir = lastCaptureFile.exists() ? lastCaptureFile.getParentDirectory()
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OSCDawServer");
        juce::FileChooser chooser("Load Capture", defaultDir, "*.xml", true);
        if (!chooser.browseForFileToOpen())
            return;

        const juce::File file = chooser.getResult();
        const bool ok = pluginManager.loadMasterTaggedMidiBufferFromFile(file);
        if (ok)
        {
            lastCaptureFile = file;
            refreshSummaryAndState();
            renderInfoLabel.setText("Capture loaded from " + file.getFullPathName(), juce::dontSendNotification);
        }
        else
        {
            renderInfoLabel.setText("Failed to load capture.", juce::dontSendNotification);
        }
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
    addAndMakeVisible(saveCaptureButton);
    addAndMakeVisible(loadCaptureButton);
    addAndMakeVisible(renderButton);
    addAndMakeVisible(openFolderButton);

    refreshSummaryAndState();
    startTimerHz(5);
}

void PreviewModal::resized()
{
    auto bounds = getLocalBounds().reduced(16);
    titleLabel.setBounds(bounds.removeFromTop(34));

    const double infoRowHeight = 26.0;
    const int infoRows = 8;
    auto infoArea = bounds.removeFromTop(static_cast<int>(infoRowHeight * infoRows + 12));

    juce::Grid infoGrid;
    infoGrid.templateColumns = { juce::Grid::TrackInfo(juce::Grid::Fr(1)) };
    infoGrid.templateRows = {};
    infoGrid.rowGap = juce::Grid::Px(4);
    infoGrid.columnGap = juce::Grid::Px(0);
    infoGrid.autoRows = { juce::Grid::TrackInfo(juce::Grid::Px(static_cast<float>(infoRowHeight))) };
    infoGrid.items = {
        juce::GridItem(totalEventsLabel),
        juce::GridItem(uniquePluginsLabel),
        juce::GridItem(durationLabel),
        juce::GridItem(noteOnLabel),
        juce::GridItem(noteOffLabel),
        juce::GridItem(ccLabel),
        juce::GridItem(otherLabel),
        juce::GridItem(transportLabel)
    };
    infoGrid.performLayout(infoArea);

    bounds.removeFromTop(24);
    auto buttonArea = bounds.removeFromTop(170);
    juce::Grid buttonGrid;
    buttonGrid.templateColumns = {
        juce::Grid::TrackInfo(juce::Grid::Fr(1)),
        juce::Grid::TrackInfo(juce::Grid::Fr(1)),
        juce::Grid::TrackInfo(juce::Grid::Fr(1))
    };
    buttonGrid.templateRows = {
        juce::Grid::TrackInfo(juce::Grid::Px(40)),
        juce::Grid::TrackInfo(juce::Grid::Px(40)),
        juce::Grid::TrackInfo(juce::Grid::Px(40))
    };
    buttonGrid.rowGap = juce::Grid::Px(8.0f);
    buttonGrid.columnGap = juce::Grid::Px(8.0f);
    buttonGrid.items = {
        juce::GridItem(playButton),
        juce::GridItem(pauseButton),
        juce::GridItem(stopButton),
        juce::GridItem(saveCaptureButton),
        juce::GridItem(loadCaptureButton),
        juce::GridItem(renderButton),
        juce::GridItem(openFolderButton),
        juce::GridItem(closeButton)
    };
    buttonGrid.performLayout(buttonArea);

    bounds.removeFromTop(24);
    renderInfoLabel.setBounds(bounds);
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
        //DBG("PreviewModal render progress label update: " << progressText);
    }

    const bool hasEvents = summary.totalEvents > 0;
    playButton.setEnabled(hasEvents && (!active || paused));
    pauseButton.setEnabled(active && !paused);
    stopButton.setEnabled(active || paused);
    renderButton.setEnabled(hasEvents && !renderJobRunning.load());
    saveCaptureButton.setEnabled(hasEvents);
    loadCaptureButton.setEnabled(true);
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
    const juce::String projectName = pluginManager.getRenderProjectName();
    launchRenderJob(lastRenderFolder, blockSize > 0 ? blockSize : 512, tailSeconds, projectName);
}

void PreviewModal::launchRenderJob(const juce::File& folder, int blockSize, double tailSeconds, juce::String projectName)
{
    if (renderJobRunning.load())
        return;

    PluginManager& pm = pluginManager;

    double sampleRate = pm.getCurrentSampleRate();
    if (sampleRate <= 0.0)
    {
        if (auto* device = pm.getDeviceManager().getCurrentAudioDevice())
            if (device != nullptr)
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
    pm.setRenderProgressCallback([safeThis](float progress)
    {
        if (auto* self = safeThis.getComponent())
        {
            juce::String progressText = "Rendering... " + juce::String(progress * 100.0f, 1) + "%";
            self->renderInfoLabel.setText(progressText, juce::dontSendNotification);
        }
    });
    pm.beginExclusiveRender(sampleRate, blockSize);

    std::thread([safeThis, &pm, folder, blockSize, tailSeconds, projectName]()
    {
        const bool ok = pm.renderMaster(folder, projectName, blockSize, tailSeconds);
        pm.clearRenderProgressCallback();
        pm.endExclusiveRender();
        juce::MessageManager::callAsync([safeThis, ok, folder]()
        {
            if (auto* self = safeThis.getComponent())
            {
                self->renderJobRunning.store(false);
                self->lastRenderFolder = folder;
                if (ok)
                    self->renderInfoLabel.setText("Render complete. Wav files saved to " + folder.getFullPathName(),
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
