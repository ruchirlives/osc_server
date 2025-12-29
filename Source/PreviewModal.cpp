#include "PreviewModal.h"
#include "RenderTimeline.h"

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

    addAndMakeVisible(playButton);
    addAndMakeVisible(pauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(closeButton);
    addAndMakeVisible(renderButton);

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
    closeButton.setBounds(buttonRow.removeFromLeft(80));

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

    const bool hasEvents = summary.totalEvents > 0;
    playButton.setEnabled(hasEvents && (!active || paused));
    pauseButton.setEnabled(active && !paused);
    stopButton.setEnabled(active || paused);
    renderButton.setEnabled(hasEvents);
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

    const double sampleRate = pluginManager.getCurrentSampleRate();
    if (sampleRate <= 0.0)
    {
        renderInfoLabel.setText("Render aborted: invalid sample rate.", juce::dontSendNotification);
        return;
    }
    const int blockSize = pluginManager.getCurrentBlockSize();
    if (blockSize <= 0)
    {
        renderInfoLabel.setText("Render aborted: invalid block size.", juce::dontSendNotification);
        return;
    }

    struct RenderScope
    {
        PluginManager& manager;
        bool active{ false };
        RenderScope(PluginManager& pm, double sr, int bs) : manager(pm)
        {
            manager.beginExclusiveRender(sr, bs);
            active = true;
        }
        ~RenderScope()
        {
            if (active)
                manager.endExclusiveRender();
        }
    };

    RenderScope scope(pluginManager, sampleRate, blockSize);

    const double renderZeroMs = pluginManager.getMasterFirstEventMs();
    auto snapshot = pluginManager.snapshotMasterTaggedMidiBuffer();
    auto renderEvents = buildRenderTimelineFromSnapshot(snapshot, renderZeroMs, sampleRate);
    constexpr double tailSeconds = 2.0;
    const auto endSample = computeEndSampleWithTail(renderEvents, sampleRate, tailSeconds);
    const double durationSeconds = sampleRate > 0.0 ? static_cast<double>(endSample) / sampleRate : 0.0;

    DBG("RenderEvents dump (" << (int)renderEvents.size() << " events)");
    for (size_t i = 0; i < renderEvents.size(); ++i)
    {
        const auto& evt = renderEvents[i];
        DBG(" [" << (int)i << "] plugin=" << evt.pluginId
            << " samplePos=" << evt.samplePos
            << " msg=" << evt.message.getDescription());
        if (i > 1000)
        {
            DBG(" ...truncated...");
            break;
        }
    }

    juce::String info = "Render prepared: " + juce::String((int)renderEvents.size())
        + " events, endSample " + juce::String(endSample)
        + ", duration " + juce::String(durationSeconds, 2) + " s";
    renderInfoLabel.setText(info, juce::dontSendNotification);

    DBG("Render folder: " + lastRenderFolder.getFullPathName());
    DBG("Render zero ms: " + juce::String(renderZeroMs, 3));
    DBG(info);
}
