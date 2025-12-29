#include "PreviewModal.h"

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

    addAndMakeVisible(playButton);
    addAndMakeVisible(pauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(closeButton);

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
    closeButton.setBounds(buttonRow.removeFromLeft(80));
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
}
