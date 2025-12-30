#include "MainComponent.h"
#include "PluginScanModal.h"
#include "PluginInstancesModal.h"
#include "RoutingModal.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

#include <thread>

class ProjectRestoreModal : public juce::Component
{
public:
	ProjectRestoreModal()
	{
		setSize(420, 64);
		statusLabel.setJustificationType(juce::Justification::centred);
		statusLabel.setFont(juce::Font(juce::FontOptions{ 14.0f, juce::Font::bold }));
		statusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
		addAndMakeVisible(statusLabel);
	}

	void paint(juce::Graphics &g) override
	{
		g.fillAll(juce::Colours::darkslategrey.darker(0.15f));
		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.drawRect(getLocalBounds(), 1);
	}

	void resized() override
	{
		statusLabel.setBounds(getLocalBounds().reduced(12, 8));
	}

	void setMessage(const juce::String &message)
	{
		statusLabel.setText(message, juce::dontSendNotification);
	}

private:
	juce::Label statusLabel{"restoreStatus", "Restoring project..."};
};

namespace
{
	class AboutContentComponent : public juce::Component
	{
	public:
		AboutContentComponent()
			: moreLink("More at github.com/ruchirlives", juce::URL("https://github.com/ruchirlives"))
		{
			infoLabel.setText(
				juce::String("Created by Ruchir Shah (c) 2024.\nBuilt on JUCE and released as open source AGPL\nOSCDawServer ") + juce::String(ProjectInfo::versionString),
				juce::dontSendNotification);
			infoLabel.setJustificationType(juce::Justification::centred);
			infoLabel.setFont(juce::Font(juce::FontOptions{ 15.0f }));

			addAndMakeVisible(infoLabel);
			addAndMakeVisible(moreLink);
		}

		void resized() override
		{
			auto bounds = getLocalBounds().reduced(16, 12);
			const int linkHeight = 28;

			auto labelBounds = bounds.removeFromTop(bounds.getHeight() - linkHeight - 6);
			infoLabel.setBounds(labelBounds);

			bounds.removeFromTop(6);
			moreLink.setBounds(bounds.removeFromTop(linkHeight));
		}

	private:
		juce::Label infoLabel;
		juce::HyperlinkButton moreLink;
	};
}

MainComponent::MainComponent()
	: tooltipWindow(this, 600)
{
	tooltipWindow.setMillisecondsBeforeTipAppears(900);
	setSize(600, 800);
	juce::LookAndFeel::setDefaultLookAndFeel(&globalLNF);

	addAndMakeVisible(audioDriverLabel);
	audioDriverLabel.setJustificationType(juce::Justification::centredLeft);

	addAndMakeVisible(audioDeviceLabel);
	audioDeviceLabel.setJustificationType(juce::Justification::centredLeft);
	audioDeviceLabel.setVisible(false);

	addAndMakeVisible(audioDeviceList);
	audioDeviceList.addListener(this);
	audioDeviceList.setTextWhenNothingSelected("Select Audio Device");
	audioDeviceList.setVisible(false);

	initAudioDrivers();

	// Initialise the Orchestra TableListBox
	addAndMakeVisible(orchestraTableWrapper);
	initOrchestraTable(); // still needed
	addDataToTable();

	// Initialize the BPM editor
	addAndMakeVisible(bpmLabel);
	bpmLabel.setText("BPM", juce::dontSendNotification);
	bpmLabel.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(bpmEditor);
	bpmEditor.setText("120"); // Default BPM
	bpmEditor.setJustification(juce::Justification::centred);
	bpmEditor.setInputRestrictions(5, "0123456789."); // Allow only numbers and a decimal point
	bpmEditor.setTooltip("Set the session tempo in beats per minute.");

	//// Initialise the audio streaming port editor
	// addAndMakeVisible(audioStreamingPortLabel);
	// audioStreamingPortLabel.setText("Audio Streaming Port", juce::dontSendNotification);
	// addAndMakeVisible(audioStreamingPortEditor);
	// audioStreamingPortEditor.setText("10000"); // Default port

	// Initialize the plugins
	initPlugins();
	initMidiInputs();

	// Initialize the "Scan" button
	addAndMakeVisible(ScanButton);
	ScanButton.onClick = [this]()
	{ showPluginScanModal(); }; // Use lambda for button click handling
	ScanButton.setTooltip("Scan the configured folder for plugins and refresh the list.");

	addAndMakeVisible(aboutButton);
	aboutButton.onClick = [this]()
	{ showAboutDialog(); };
	aboutButton.setTooltip("Show version information and project links.");

	// Initialize the "Get Recorded" button
	addAndMakeVisible(getRecordedButton);
	getRecordedButton.onClick = [this]()
	{ midiManager.getRecorded(); updateOverdubUI(); }; // Use lambda for button click handling
	getRecordedButton.setTooltip("Fetch and clear the recorded overdub buffer.");

	// Initialize the "List Plugin Instances" button
	addAndMakeVisible(listPluginInstancesButton);
	listPluginInstancesButton.onClick = [this]()
	{ showPluginInstancesModal(); }; // Use lambda for button click handling
	listPluginInstancesButton.setTooltip("Display every plugin instance and its ID.");

	addAndMakeVisible(routingButton);
	routingButton.onClick = [this]()
	{ showRoutingModal(); };
	routingButton.setTooltip("Configure stems and match rules for the audio router.");

	// Initialize the "Send Test Note" button
	addAndMakeVisible(sendTestNoteButton);
	sendTestNoteButton.onClick = [this]()
	{ midiManager.sendTestNote(); }; // Use lambda for button click handling
	sendTestNoteButton.setTooltip("Send a short MIDI ping through the selected instrument.");

	// Initialize the "Open Plugin" button
	addAndMakeVisible(openPluginButton);
	openPluginButton.onClick = [this]()
	{ openPlugins(orchestraTable); }; // Use lambda for button click handling
	openPluginButton.setTooltip("Open the plugin UI for the selected instruments.");

	// Initialize the "Add Instrument" and "Remove Instrument" buttons
	addAndMakeVisible(addInstrumentButton);
	addInstrumentButton.onClick = [this]()
	{ addInstrument(); }; // Use lambda for button click handling
	addInstrumentButton.setTooltip("Duplicate the selected instrument slot.");

	// Initialize Add New Instrument button
	addAndMakeVisible(addNewInstrumentButton);
	addNewInstrumentButton.onClick = [this]()
	{ addNewInstrument(); }; // Use lambda for button click handling
	addNewInstrumentButton.setTooltip("Create a new instrument entry.");

	// Initialize the "Remove Instrument" button
	addAndMakeVisible(removeInstrumentButton);
	removeInstrumentButton.onClick = [this]()
	{ removeInstrument(); }; // Use lambda for button click handling
	removeInstrumentButton.setTooltip("Remove the selected instruments from the orchestra.");

	// Initialize the "Save" and "Restore" buttons
	addAndMakeVisible(saveButton);
	saveButton.onClick = [this]()
	{ saveProject(); }; // Use lambda for button click handling
	saveButton.setTooltip("Save the current project to an .oscdaw file.");

	addAndMakeVisible(restoreButton);
	restoreButton.onClick = [this]()
	{ restoreProject(); }; // Use lambda for button click handling
	restoreButton.setTooltip("Load or append a project from disk.");

	// Add Project name label
	addAndMakeVisible(projectNameLabel);

	// Move selected rows to end of orchestra
	addAndMakeVisible(moveToEndButton);
	moveToEndButton.onClick = [this]()
	{ moveSelectedRowsToEnd(); }; // Use lambda for button click handling
	moveToEndButton.setTooltip("Move the selected instruments to the end of the table.");

	// Initialize the "Start Overdub" button
	addAndMakeVisible(startOverdubButton);
	startOverdubButton.onClick = [this]()
	{ midiManager.startOverdub(); updateOverdubUI(); };
	startOverdubButton.setTooltip("Begin recording MIDI into the overdub buffer.");

	// Initialize the "Stop Overdub" button
	addAndMakeVisible(stopOverdubButton);
	stopOverdubButton.onClick = [this]()
	{ midiManager.stopOverdub(); updateOverdubUI(); };
	stopOverdubButton.setTooltip("Stop the active overdub take.");

	// Initialize the "Play Overdub" button
	addAndMakeVisible(playOverdubButton);
	playOverdubButton.onClick = [this]()
	{ midiManager.playOverdub(); updateOverdubUI(); };
	playOverdubButton.setTooltip("Play back the captured overdub buffer.");

	// Initialize the "Bake Overdub" button
	addAndMakeVisible(bakeOverdubButton);
	bakeOverdubButton.onClick = [this]()
	{ midiManager.bakeOverdubIntoMaster(); updateOverdubUI(); };
	bakeOverdubButton.setTooltip("Merge the overdub buffer into the master capture.");

	// Initialize the "Play Capture" button
	addAndMakeVisible(playCaptureButton);
	playCaptureButton.onClick = [this]()
	{
		DBG("Starting preview playback of captured master buffer");
		pluginManager.previewPlay();
		updateOverdubUI();
	};
	playCaptureButton.setTooltip("Play the captured master buffer.");

	// Initialize the "Stop Capture" button
	addAndMakeVisible(stopCaptureButton);
	stopCaptureButton.onClick = [this]()
	{
		pluginManager.previewStop();
		updateOverdubUI();
	};
	stopCaptureButton.setTooltip("Stop playback of the captured master buffer.");

	// playOverdubOnTriggerButton
	addAndMakeVisible(triggerOverdubButton);
	triggerOverdubButton.onClick = [this]()
	{ midiManager.triggerOverdub(); updateOverdubUI(); };
	triggerOverdubButton.setTooltip("Arm overdub playback to trigger later.");

	// Initialize the "Strip Silence" button
	addAndMakeVisible(stripLeadingSilenceButton);
	stripLeadingSilenceButton.onClick = [this]()
	{ midiManager.stripLeadingSilence(); updateOverdubUI(); };
	stripLeadingSilenceButton.setTooltip("Remove silence at the start of the overdub take.");

	// Initialize the "Undo Overdub" button
	addAndMakeVisible(undoOverdubButton);
	undoOverdubButton.onClick = [this]()
	{ midiManager.undoLastOverdub(); updateOverdubUI(); };
	undoOverdubButton.setTooltip("Revert the most recent overdub pass.");

	addAndMakeVisible(importMidiButton);
	importMidiButton.onClick = [this]()
	{ midiManager.importMidiFileToRecordBuffer(); updateOverdubUI(); };
	importMidiButton.setTooltip("Import a MIDI file into the overdub buffer.");

	addAndMakeVisible(exportMidiButton);
	exportMidiButton.onClick = [this]()
	{ midiManager.exportRecordBufferToMidiFile(); };
	exportMidiButton.setTooltip("Export the overdub buffer as a MIDI file.");

	// Set up config file path
	juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OSCDawServer");
	if (!dawServerDir.exists())
		dawServerDir.createDirectory();
	configFile = dawServerDir.getChildFile("config.ini");

	loadConfig();

	resized();
	updateOverdubUI();

	juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]()
									{
										if (auto* self = safe.getComponent())
										{
											if (self->onInitialised)
												self->onInitialised();
										} });
}

MainComponent::~MainComponent()
{
	saveConfig();
	pluginManager.releaseResources();
	midiManager.closeMidiInput();
	conductor.shutdown(); // <--- important for clean exit
	juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainComponent::resized()
{
	// --- Adjustable layout constants ---
	auto metrics = getLayoutMetrics();
	const int margin = metrics.margin;
	const int buttonWidth = metrics.buttonWidth;
	const int buttonHeight = metrics.buttonHeight;
	const int spacingX = metrics.spacingX;
	const int spacingY = metrics.spacingY;
	const int labelHeight = metrics.labelHeight;
	const int numButtonRows = metrics.numButtonRows;

	const int windowWidth = getWidth();
	const int windowHeight = getHeight();

	// --- Top controls ---
	const int topRowY = margin;
	auto nextX = margin;
	const auto placeTopControl = [&](juce::Component &comp, int width)
	{
		comp.setBounds(nextX, topRowY, width, labelHeight);
		nextX += width + spacingX;
	};

	juce::GlyphArrangement projectNameGlyphs;
	const float projectNameHeight = static_cast<float>(labelHeight);
	projectNameGlyphs.addFittedText(projectNameLabel.getFont(),
		projectNameLabel.getText(),
		0.0f,
		0.0f,
		static_cast<float>(windowWidth),
		projectNameHeight,
		juce::Justification::centredLeft,
		1,
		1.0f);
	const auto projectBoundingBox = projectNameGlyphs.getBoundingBox(0, projectNameGlyphs.getNumGlyphs(), true);
	const int projectNameTextWidth = juce::jmax(0, juce::roundToInt(projectBoundingBox.getWidth()));
	const int projectNameWidth = juce::jlimit(buttonWidth, windowWidth / 2, projectNameTextWidth + 200);
	const int audioPortLabelWidth = 150;
	const int audioPortFieldWidth = 100;
	const int bpmLabelWidth = 38;
	const int bpmFieldWidth = 90;
	const int bpmSpacing = spacingX / 2;

	placeTopControl(projectNameLabel, projectNameWidth);
	placeTopControl(audioStreamingPortLabel, audioPortLabelWidth);
	placeTopControl(audioStreamingPortEditor, audioPortFieldWidth);

	const int bpmEditorRight = windowWidth - margin - 110;
	const int bpmEditorX = bpmEditorRight - bpmFieldWidth;
	bpmEditor.setBounds(bpmEditorX, topRowY, bpmFieldWidth, labelHeight);
	bpmLabel.setBounds(bpmEditorX - bpmSpacing - bpmLabelWidth, topRowY, bpmLabelWidth, labelHeight);

	const int aboutButtonWidth = 110;
	aboutButton.setBounds(windowWidth - margin - aboutButtonWidth, topRowY, aboutButtonWidth, labelHeight);

	const int driverRowY = projectNameLabel.getBottom() + spacingY / 2;
	audioDriverLabel.setBounds(margin, driverRowY, 150, labelHeight);
	audioDriverList.setBounds(audioDriverLabel.getRight() + spacingX, driverRowY, 200, labelHeight);

	// Audio device selection row (only visible when devices are available)
	audioDeviceLabel.setBounds(audioDriverList.getRight() + spacingX, driverRowY, 120, labelHeight);
	audioDeviceList.setBounds(audioDeviceLabel.getRight() + spacingX, driverRowY, 200, labelHeight);

	bpmEditor.setJustification(juce::Justification::centred);
	audioStreamingPortEditor.setJustification(juce::Justification::centred);
	audioDriverList.setJustificationType(juce::Justification::centredLeft);

	// --- Reserved space at bottom for buttons ---
	const int totalButtonHeight = numButtonRows * buttonHeight + (numButtonRows - 1) * spacingY;
	const int buttonAreaTop = windowHeight - totalButtonHeight - margin;

	// --- Table area: from bottom of top controls to top of button area ---
	int topControlsBottom = projectNameLabel.getBottom();
	topControlsBottom = juce::jmax(topControlsBottom, audioStreamingPortEditor.getBottom());
	topControlsBottom = juce::jmax(topControlsBottom, audioDriverList.getBottom());
	const int tableTop = topControlsBottom + spacingY;
	const int tableHeight = buttonAreaTop - tableTop - spacingY; // extra spacing between table and buttons
	orchestraTableWrapper.setBounds(margin, tableTop, windowWidth - 2 * margin, tableHeight);

	auto tablePanelBounds = computeTablePanelBounds(metrics, orchestraTableWrapper.getBounds());
	auto buttonLayout = computeButtonPanelLayout(metrics, tablePanelBounds);

	// --- Button rows, starting from bottom upward ---

	// Row 1 (bottom row) - Scan, Select Plugin, Update, Open Plugin, List Plugin Instances
	const int row1Y = buttonLayout.rowY[3];
	const int row1Buttons = 5;
	juce::ignoreUnused(row1Buttons);
	const int row1ButtonWidth = buttonWidth;

	int currentX = margin;
	auto placeRowButton = [&](juce::Component &button)
	{
		button.setBounds(currentX, row1Y, row1ButtonWidth, buttonHeight);
		currentX += row1ButtonWidth + spacingX;
	};

	placeRowButton(ScanButton);
	placeRowButton(pluginBox);
	placeRowButton(openPluginButton);
	placeRowButton(listPluginInstancesButton);
	placeRowButton(routingButton);

	// Row 2 - Instrument management and recording
	const int row2Y = buttonLayout.rowY[2];
	addInstrumentButton.setBounds(margin, row2Y, buttonWidth, buttonHeight);
	addNewInstrumentButton.setBounds(addInstrumentButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	removeInstrumentButton.setBounds(addNewInstrumentButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	moveToEndButton.setBounds(removeInstrumentButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	getRecordedButton.setBounds(moveToEndButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);

	// Row 3 - MIDI and utility controls
	const int row3Y = buttonLayout.rowY[1];
	sendTestNoteButton.setBounds(margin, row3Y, buttonWidth, buttonHeight);
	importMidiButton.setBounds(sendTestNoteButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	exportMidiButton.setBounds(importMidiButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	midiInputList.setBounds(exportMidiButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	stripLeadingSilenceButton.setBounds(midiInputList.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);

	// Row 4 (top row of buttons) - Project and overdub controls
	const int row4Y = buttonLayout.rowY[0];
	saveButton.setBounds(margin, row4Y, buttonWidth, buttonHeight);
	restoreButton.setBounds(saveButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);
	const int miniButtonAvailableWidth = buttonWidth - 2 * spacingX;
	const int miniButtonWidth = juce::jmax(1, miniButtonAvailableWidth / 3);
	startOverdubButton.setBounds(restoreButton.getRight() + spacingX, row4Y, miniButtonWidth, buttonHeight);
	triggerOverdubButton.setBounds(startOverdubButton.getRight() + spacingX, row4Y, miniButtonWidth, buttonHeight);
	playOverdubButton.setBounds(triggerOverdubButton.getRight() + spacingX, row4Y, miniButtonWidth, buttonHeight);
	stopOverdubButton.setBounds(playOverdubButton.getRight() + spacingX, row4Y, miniButtonWidth, buttonHeight);
	bakeOverdubButton.setBounds(stopOverdubButton.getRight() + spacingX, row4Y, miniButtonWidth, buttonHeight);
	undoOverdubButton.setBounds(bakeOverdubButton.getRight() + spacingX, row4Y, miniButtonWidth, buttonHeight);
	const int captureButtonWidth = juce::jmax(1, (buttonWidth - spacingX) / 2);
	playCaptureButton.setBounds(undoOverdubButton.getRight() + spacingX, row4Y, captureButtonWidth, buttonHeight);
	stopCaptureButton.setBounds(playCaptureButton.getRight() + spacingX, row4Y, captureButtonWidth, buttonHeight);

	// --- BPM Sync handler ---
	bpmEditor.onTextChange = [this]()
	{
		pluginManager.setBpm(getBpm());
	};

	// --- Audio Streaming Port handler ---
	audioStreamingPortEditor.onFocusLost = [this]()
	{
		handleAudioPortChange();
	};

	audioStreamingPortEditor.onReturnKey = [this]()
	{
		handleAudioPortChange();
	};
}

void MainComponent::updateOverdubUI()
{
	if (midiManager.isOverdubbing)
	{
		startOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
		stopOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
	}
	else
	{
		startOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::lightgrey);
		stopOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::lightgrey);
	}

	if (midiManager.isStripped)
		stripLeadingSilenceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::lightgrey);
	else
		stripLeadingSilenceButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);

	if (midiManager.playOverdubOnTriggerArmed)
		triggerOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
	else
		triggerOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::lightgrey);

	stripLeadingSilenceButton.setEnabled(!midiManager.isOverdubbing && midiManager.hasRecordedEvents());
	undoOverdubButton.setEnabled(!midiManager.isOverdubbing && midiManager.canUndoOverdub());
	bakeOverdubButton.setEnabled(!midiManager.isOverdubbing && midiManager.hasRecordedEvents());
	if (!midiManager.isOverdubbing && midiManager.hasRecordedEvents())
		bakeOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
	else
		bakeOverdubButton.setColour(juce::TextButton::buttonColourId, juce::Colours::lightgrey);
	const bool captureHasEvents = pluginManager.hasMasterTaggedMidiData();
	const bool previewActive = pluginManager.isPreviewActive();
	const bool previewPaused = pluginManager.isPreviewPaused();
	const bool shouldEnablePlayCapture = captureHasEvents && (!previewActive || previewPaused);
	playCaptureButton.setEnabled(shouldEnablePlayCapture);
	if (previewActive && !previewPaused)
		playCaptureButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
	else
		playCaptureButton.setColour(juce::TextButton::buttonColourId, shouldEnablePlayCapture ? juce::Colours::orange : juce::Colours::lightgrey);
	DBG("Play Capture button enabled=" << (shouldEnablePlayCapture ? "true" : "false")
		<< " captureHasEvents=" << (captureHasEvents ? "true" : "false")
		<< " previewActive=" << (previewActive ? "true" : "false")
		<< " previewPaused=" << (previewPaused ? "true" : "false"));
	stopCaptureButton.setEnabled(previewActive || previewPaused);
}
void MainComponent::handleAudioPortChange()
{
	juce::String portText = audioStreamingPortEditor.getText();
	int port = portText.getIntValue();

	if (port > 0 && port < 65536)
	{
		if (audioStreamer)
		{
			audioStreamer->setPort(port);
			DBG("Audio Streaming Port set to: " + juce::String(port));
		}
	}
	else
	{
		DBG("Invalid Audio Streaming Port: " + portText);
		audioStreamingPortEditor.setText("10000", juce::dontSendNotification); // Reset to default
	}
}

void MainComponent::moveSelectedRowsToEnd()
{
	// Collect the selected rows
	auto selectedRows = orchestraTable.getSelectedRows();
	std::vector<InstrumentInfo> instrumentsToMove; // Replace InstrumentType with the actual type of your instruments

	// Collect the instruments to move in the order of selected rows
	for (int i = 0; i < selectedRows.size(); ++i)
	{
		int row = selectedRows[i];
		instrumentsToMove.push_back(conductor.orchestra[row]);
	}

	// Remove the instruments from the orchestra in reverse order to avoid shifting
	for (int i = selectedRows.size() - 1; i >= 0; --i)
	{
		int row = selectedRows[i];
		conductor.orchestra.erase(conductor.orchestra.begin() + row);
	}

	// Append the collected instruments to the end of the orchestra in the original selection order
	conductor.orchestra.insert(conductor.orchestra.end(), instrumentsToMove.begin(), instrumentsToMove.end());

	// Update the orchestra table
	orchestraTable.updateContent();
}

void MainComponent::updateProjectNameLabel(juce::String projectName)
{
	currentProjectName = projectName.trim();
	projectNameLabel.setText("Project Name: " + currentProjectName, juce::dontSendNotification);
}

juce::String MainComponent::getCurrentProjectName() const
{
	return currentProjectName.isNotEmpty() ? currentProjectName : "Capture";
}

void MainComponent::saveProject(const std::vector<InstrumentInfo> &selectedInstruments)
{
	// Create OSCDawServer subfolder in user's documents directory
	juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OSCDawServer");
	if (!dawServerDir.exists())
		dawServerDir.createDirectory();

	// Get the full file paths in OSCDawServer subfolder

	juce::File dataFile = dawServerDir.getChildFile("projectData.dat");
	juce::File pluginsFile = dawServerDir.getChildFile("projectPlugins.dat");
	juce::File metaFile = dawServerDir.getChildFile("projectMeta.xml");
	juce::File routingFile = dawServerDir.getChildFile("projectRouting.xml");
	juce::File captureBufferFile = dawServerDir.getChildFile("projectTaggedMidiBuffer.xml");
	const bool includeRoutingData = selectedInstruments.empty();
	if (includeRoutingData)
	{
		if (!pluginManager.saveRoutingConfigToFile(routingFile))
			DBG("Warning: Failed to write routing configuration file.");
	}

	// Save the project state files
	conductor.saveAllData(dataFile.getFullPathName(), pluginsFile.getFullPathName(), metaFile.getFullPathName(), selectedInstruments);
	const bool captureBufferSaved = includeRoutingData && pluginManager.hasMasterTaggedMidiData() && pluginManager.saveMasterTaggedMidiBufferToFile(captureBufferFile);

	// Open a file chooser dialog for the custom project file
	juce::FileChooser fileChooser("Save Project", juce::File(), "*.oscdaw"); // Custom suffix here

	if (fileChooser.browseForFileToSave(true))
	{
		// Get the selected file, ensuring it has the correct suffix
		juce::File customFile = fileChooser.getResult().withFileExtension(".oscdaw");

		// Delete file if it already exists
		if (customFile.exists())
		{
			customFile.deleteFile();
		}

		juce::FileOutputStream outputStream(customFile);

		if (outputStream.openedOk())
		{
			juce::ZipFile::Builder zipBuilder;
			zipBuilder.addFile(dataFile, 5, "projectData.dat"); // Using moderate compression
			zipBuilder.addFile(pluginsFile, 5, "projectPlugins.dat");
			zipBuilder.addFile(metaFile, 1, "projectMeta.xml");
			if (includeRoutingData && routingFile.existsAsFile())
				zipBuilder.addFile(routingFile, 1, "projectRouting.xml");
			if (captureBufferSaved && captureBufferFile.existsAsFile())
				zipBuilder.addFile(captureBufferFile, 1, "projectTaggedMidiBuffer.xml");

			// Write the compressed data to the file
			zipBuilder.writeToStream(outputStream, nullptr);
		}
	}
	// Get the saved file name
	juce::String projectName = fileChooser.getResult().getFileNameWithoutExtension();
	DBG("Project Saved: " + projectName);
	updateProjectNameLabel(projectName);
}

void MainComponent::restoreProject(bool append)
{
	juce::FileChooser fileChooser("Open Project", juce::File(), "*.oscdaw");
	if (!fileChooser.browseForFileToOpen())
		return;

	auto *statusComponent = new ProjectRestoreModal();
	statusComponent->setSize(420, 64);
	juce::DialogWindow::LaunchOptions opts;
	opts.content.setOwned(statusComponent);
	opts.dialogTitle = "Restoring Project";
	opts.dialogBackgroundColour = juce::Colours::darkslategrey;
	opts.useNativeTitleBar = true;
	opts.escapeKeyTriggersCloseButton = false;
	opts.resizable = false;
	opts.launchAsync();

	auto safeStatus = juce::Component::SafePointer<ProjectRestoreModal>(statusComponent);
	auto updateStatus = [safeStatus](const juce::String &message)
	{
		auto deliver = [safeStatus, message]() mutable
		{
			if (auto *comp = safeStatus.getComponent())
				comp->setMessage(message);
		};

		if (juce::MessageManager::getInstance()->isThisTheMessageThread())
			deliver();
		else
			juce::MessageManager::callAsync(std::move(deliver));
	};

	auto closeStatus = [safeStatus]()
	{
		juce::MessageManager::callAsync([safeStatus]()
										{
			if (auto* comp = safeStatus.getComponent())
			{
				if (auto* dialog = comp->findParentComponentOfClass<juce::DialogWindow>())
					dialog->exitModalState(0);
			} });
	};

	auto runOnMessageThreadBlocking = [](std::function<void()> fn)
	{
		if (juce::MessageManager::getInstance()->isThisTheMessageThread())
		{
			fn();
			return;
		}
		juce::WaitableEvent done;
		juce::MessageManager::callAsync([fn = std::move(fn), &done]() mutable
										{
			fn();
			done.signal(); });
		done.wait();
	};

	const juce::File zipFile = fileChooser.getResult();
	updateStatus("Selected Project: " + zipFile.getFileName());
	DBG("Selected Project: " + zipFile.getFullPathName());

	pluginManager.setRestoreStatusCallback([updateStatus](const juce::String &message)
										   { updateStatus(message); });

	std::thread([this, zipFile, updateStatus, closeStatus, runOnMessageThreadBlocking, append]()
				{
		bool restoreSucceeded = false;
		juce::FileInputStream inputStream(zipFile);
		if (inputStream.openedOk())
		{
			updateStatus("Reading Project...");
			DBG("Reading Project...");
			juce::ZipFile zip(inputStream);
			updateStatus("Project Read.");
			DBG("Project Read.");

			juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OSCDawServer");
			if (!dawServerDir.exists())
				dawServerDir.createDirectory();

			juce::File dataFile = dawServerDir.getChildFile("projectData.dat");
			juce::File pluginsFile = dawServerDir.getChildFile("projectPlugins.dat");
			juce::File metaFile = dawServerDir.getChildFile("projectMeta.xml");
			juce::File routingFile = dawServerDir.getChildFile("projectRouting.xml");
			juce::File bufferFile = dawServerDir.getChildFile("projectTaggedMidiBuffer.xml");

			updateStatus("Unzipping Project...");
			auto extractFile = [&](const juce::String& fileName, const juce::File& destination)
			{
				auto index = zip.getIndexOfFileName(fileName);
				if (index >= 0)
				{
					auto* fileStream = zip.createStreamForEntry(index);
					if (fileStream != nullptr)
					{
						if (destination.exists())
							destination.deleteFile();
						juce::FileOutputStream outStream(destination);
						if (outStream.openedOk())
							outStream.writeFromInputStream(*fileStream, -1);
						delete fileStream;
						return true;
					}
				}
				return false;
			};

			DBG("Unzipping Project...");
			extractFile("projectData.dat", dataFile);
			extractFile("projectPlugins.dat", pluginsFile);
			extractFile("projectMeta.xml", metaFile);
			const bool routingExtracted = extractFile("projectRouting.xml", routingFile);
			const bool bufferExtracted = extractFile("projectTaggedMidiBuffer.xml", bufferFile);
			updateStatus("Project Unzipped.");
			DBG("Project Unzipped.");

			runOnMessageThreadBlocking([this, &dataFile, &pluginsFile, &metaFile, routingExtracted, &routingFile, bufferExtracted, &bufferFile, append]()
			{
				if (!append)
				{
					conductor.restoreAllData(dataFile.getFullPathName(), pluginsFile.getFullPathName(), metaFile.getFullPathName());
					if (routingExtracted)
						pluginManager.loadRoutingConfigFromFile(routingFile);
					if (bufferExtracted)
					{
						if (!pluginManager.loadMasterTaggedMidiBufferFromFile(bufferFile))
							pluginManager.clearMasterTaggedMidiBuffer();
					}
					else
					{
						pluginManager.clearMasterTaggedMidiBuffer();
					}
				}
				else
				{
					conductor.upsertAllData(dataFile.getFullPathName(), pluginsFile.getFullPathName(), metaFile.getFullPathName());
				}
				pluginManager.rebuildRouterTagIndexFromConductor();
			});

			runOnMessageThreadBlocking([this, &zipFile]()
			{
				refreshOrchestraTableUI();
				const juce::String projectName = zipFile.getFileNameWithoutExtension();
				DBG("Project Restored: " + projectName);
				updateProjectNameLabel(projectName);
				repaint();
				updateOverdubUI();
			});

			restoreSucceeded = true;
		}
		else
		{
			updateStatus("Failed to open project file.");
			DBG("Failed to open file for restoring project states.");
		}

		if (!restoreSucceeded)
		{
			runOnMessageThreadBlocking([this]()
			{
				repaint();
				if (auto* top = getTopLevelComponent())
				{
					auto w = top->getWidth();
					auto h = top->getHeight();
					top->setSize(w + 1, h);
					top->setSize(w, h);
				}
			});
		}

		pluginManager.clearRestoreStatusCallback();
		closeStatus(); })
		.detach();
}

void MainComponent::refreshOrchestraTableUI()
{
	orchestraTable.updateContent();
	resized();
	repaint();

	if (auto *top = getTopLevelComponent())
	{
		auto w = top->getWidth();
		auto h = top->getHeight();
		top->setSize(w + 1, h);
		top->setSize(w, h);
	}
}

void MainComponent::addDataToTable()
{
	// Add the instrument to the orchestra
	// fill orchestra with dummy data
	InstrumentInfo instrument1;
	instrument1.instrumentName = "My Instrument";
	instrument1.pluginName = "Click Select Plugin button below --->";
	instrument1.pluginInstanceId = "Selection 1";
	instrument1.midiChannel = 1;
	instrument1.tags.push_back("myTag");

	conductor.orchestra.push_back(instrument1);
}

void MainComponent::openPlugins(juce::TableListBox &table)
{
	// Get the selected rows
	auto selectedRows = table.getSelectedRows();

	// Open the plugin window for each selected row by iterating through the juce::SparseSet
	for (int i = 0; i < selectedRows.size(); ++i)
	{
		int row = selectedRows[i];
		// Get the plugin instance ID from the selected row
		auto pluginInstanceId = orchestraTableModel.getText(3, row);
		DBG("Opening Plugin Window for Plugin Instance ID: " + pluginInstanceId);

		// Open the plugin window for the selected plugin instance ID
		pluginManager.openPluginWindow(pluginInstanceId);
	}
}

void MainComponent::initOrchestraTable()
{
	// Set the model for the TableListBox to the orchestraTableModel
	orchestraTable.setModel(&orchestraTableModel);
	orchestraTable.setMultipleSelectionEnabled(true);
	orchestraTable.getHeader().addColumn("Instrument Name", 1, 150);
	orchestraTable.getHeader().addColumn("Plugin Name", 2, 150);
	orchestraTable.getHeader().addColumn("Plugin Instance ID", 3, 150);
	orchestraTable.getHeader().addColumn("MIDI Channel", 4, 100);
	orchestraTable.getHeader().addColumn("Tags", 5, 200);
}

void MainComponent::addInstrument()
{
	// Add a new instrument to the orchestra
	InstrumentInfo instrument;
	int newRow = -1; // We'll use this to keep track of the new instrument's row index

	// Get the previous instrument if it exists
	if (conductor.orchestra.size() > 0)
	{
		// First try to get the selected instrument in the orchestra
		auto selectedRows = orchestraTable.getSelectedRows();
		if (selectedRows.size() > 0)
		{
			int row = selectedRows[0];
			instrument = conductor.orchestra[row];

			// Increment the MIDI channel
			instrument.midiChannel++;

			// Insert the new instrument after the selected row
			newRow = row + 1;
			conductor.orchestra.insert(conductor.orchestra.begin() + newRow, instrument);

			// Update and select the new row
			UpdateAndSelect(newRow);
		}
		else
		{
			// If no instrument is selected, get the previous instrument
			auto &previousInstrument = conductor.orchestra.back();
			// Copy the previous instrument's values
			instrument = previousInstrument;

			// Increment the MIDI channel
			instrument.midiChannel++;
			newRow = static_cast<int>(conductor.orchestra.size());
			conductor.orchestra.push_back(instrument);

			// Update and select the last row
			UpdateAndSelect(newRow - 1);
		}
	}
	else
	{
		// Set the default values for the new instrument
		basicInstrument(instrument);
		conductor.orchestra.push_back(instrument);
		newRow = static_cast<int>(conductor.orchestra.size()) - 1;

		// Update and select the last row
		UpdateAndSelect(newRow);
	}

	// Set the tag for the new instrument from the text on the Windows clipboard
	pasteClipboard(newRow);
}

void MainComponent::pasteClipboard(int newRow)
{
	juce::String clipboardText = juce::SystemClipboard::getTextFromClipboard();
	DBG("ClipboardText: " << clipboardText);
	if (!clipboardText.isEmpty() && newRow >= 0)
	{
		// Update the instrument directly in the orchestra vector
		conductor.orchestra[newRow].tags.clear();
		conductor.orchestra[newRow].tags.push_back(clipboardText);

		// Send the tags to the OSC sender
		conductor.sendOSCMessage(conductor.orchestra[newRow].tags);

		// Optionally, update the row display if necessary
		orchestraTable.updateContent();
	}
}

void MainComponent::addNewInstrument()
{
	// Add a new instrument to the orchestra
	InstrumentInfo instrument;
	int newRow = -1; // We'll use this to keep track of the new instrument's row index
	// Set the default values for the new instrument
	basicInstrument(instrument);
	conductor.orchestra.push_back(instrument);
	newRow = static_cast<int>(conductor.orchestra.size()) - 1;
	// Update and select the last row
	UpdateAndSelect(newRow);

	// Set the tag for the new instrument from the text on the Windows clipboard
	pasteClipboard(newRow);
}

void MainComponent::UpdateAndSelect(int row)
{
	// Update the orchestra table
	orchestraTable.updateContent();

	// Select the new instrument
	orchestraTable.selectRow(row);
}

void MainComponent::basicInstrument(InstrumentInfo &instrument)
{
	instrument.instrumentName = "New Instrument";
	instrument.pluginName = "New Plugin";
	instrument.midiChannel = 1;

	instrument.tags.clear();
	instrument.tags.push_back("Tag " + juce::String(getNextTagNumber()));
	instrument.pluginInstanceId = "Instance " + juce::String(getNextInstanceNumber());
}

void MainComponent::removeInstrument()
{
	// Remove the selected instrument from the orchestra
	auto selectedRows = orchestraTable.getSelectedRows();
	// Remove in reverse order
	for (int i = selectedRows.size() - 1; i >= 0; --i)
	{
		int row = selectedRows[i];
		conductor.orchestra.erase(conductor.orchestra.begin() + row);
	}

	// Update the orchestra table
	orchestraTable.updateContent();
}

bool MainComponent::getFolder()
{
	juce::FileChooser fileChooser("Select a directory with plugins", pluginFolder);
	if (fileChooser.browseForDirectory())
	{
		DBG("Scanning for plugins VST3...");
		pluginFolder = fileChooser.getResult();
		return pluginFolder.isDirectory();
	}

	DBG("No folder selected");
	return false;
}

void MainComponent::paint(juce::Graphics &g)
{
	auto base = findColour(juce::ResizableWindow::backgroundColourId);
	auto gradient = juce::ColourGradient(base.brighter(0.1f), 0, 0,
										 base.darker(0.2f), 0, (float)getHeight(), false);
	g.setGradientFill(gradient);
	g.fillAll();

	auto metrics = getLayoutMetrics();
	const auto tablePanelBounds = computeTablePanelBounds(metrics, orchestraTableWrapper.getBounds());
	const auto buttonLayout = computeButtonPanelLayout(metrics, tablePanelBounds);

	if (!tablePanelBounds.isEmpty())
	{
		auto tableGradientStart = tablePanelBounds.getPosition();
		auto tableGradientEnd = tablePanelBounds.getBottomLeft();
		auto colour = base.brighter(0.08f);
		auto tableGradient = juce::ColourGradient(colour.brighter(0.1f),
												  tableGradientStart.x, tableGradientStart.y,
												  colour.darker(0.15f), tableGradientEnd.x, tableGradientEnd.y, false);
		g.setGradientFill(tableGradient);
		g.fillRoundedRectangle(tablePanelBounds, 12.0f);
		g.setColour(juce::Colours::white.withAlpha(0.15f));
		g.drawRoundedRectangle(tablePanelBounds, 12.0f, 2.0f);
	}

	if (!buttonLayout.panel.isEmpty())
	{
		auto panelColour = base.brighter(0.05f);
		g.setColour(panelColour);
		g.fillRoundedRectangle(buttonLayout.panel, 10.0f);
		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.drawRoundedRectangle(buttonLayout.panel, 10.0f, 2.0f);
	}
}

double MainComponent::getBpm() const
{
	return bpmEditor.getText().getDoubleValue(); // Or however you're storing user input
}

void MainComponent::setBpm(double bpm)
{
	bpmEditor.setText(juce::String(bpm, 3), juce::dontSendNotification);
}

void MainComponent::showPluginInstancesModal()
{
	juce::DialogWindow::LaunchOptions options;
	options.dialogTitle = "Plugin Instances";
	options.dialogBackgroundColour = findColour(juce::ResizableWindow::backgroundColourId);
	options.escapeKeyTriggersCloseButton = true;
	options.useNativeTitleBar = true;
	options.resizable = false;
	options.componentToCentreAround = this;

	auto *modalContent = new PluginInstancesModal(
		pluginManager,
		[this](const juce::String &oldId, const juce::String &newId)
		{
			updatePluginInstanceReferences(oldId, newId);
		});
	modalContent->setSize(420, 360);
	options.content.setOwned(modalContent);
	options.launchAsync();
}

void MainComponent::showRoutingModal()
{
	juce::DialogWindow::LaunchOptions options;
	options.dialogTitle = "Routing";
	options.dialogBackgroundColour = findColour(juce::ResizableWindow::backgroundColourId);
	options.escapeKeyTriggersCloseButton = true;
	options.useNativeTitleBar = true;
	options.resizable = true;
	options.componentToCentreAround = this;

	auto *modalContent = new RoutingModal(pluginManager);
	modalContent->setSize(640, 420);
	options.content.setOwned(modalContent);
	options.launchAsync();
}

void MainComponent::showAboutDialog()
{
	auto content = std::make_unique<AboutContentComponent>();
	content->setSize(320, 150);
	juce::CallOutBox::launchAsynchronously(std::move(content), aboutButton.getScreenBounds(), nullptr);
}

void MainComponent::updatePluginInstanceReferences(const juce::String &oldId, const juce::String &newId)
{
	bool changed = false;
	for (auto &instrument : conductor.orchestra)
	{
		if (instrument.pluginInstanceId == oldId)
		{
			instrument.pluginInstanceId = newId;
			changed = true;
		}
	}

	if (changed)
	{
		orchestraTable.updateContent();
	}
}

void MainComponent::showPluginScanModal()
{
	juce::DialogWindow::LaunchOptions options;
	options.dialogTitle = "Plugin Scanner";
	options.dialogBackgroundColour = findColour(juce::ResizableWindow::backgroundColourId);
	options.escapeKeyTriggersCloseButton = true;
	options.useNativeTitleBar = true;
	options.resizable = false;
	options.componentToCentreAround = this;

	auto *modalContent = new PluginScanModal(
		pluginManager,
		[this]()
		{ scanForPlugins(PluginScanMode::Replace); },
		[this]()
		{ scanForPlugins(PluginScanMode::Add); },
		[this]()
		{ initPluginsList(); });

	modalContent->setSize(450, 360);
	options.content.setOwned(modalContent);
	options.launchAsync();
}

void MainComponent::replacePluginForRow(int row, juce::Component* anchor)
{
	if (row < 0 || row >= static_cast<int>(conductor.orchestra.size()))
		return;

	const auto types = pluginManager.knownPluginList.getTypes();
	if (types.isEmpty())
	{
		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
			"Replace Plugin", "No plugins are available to replace with.");
		return;
	}

	juce::PopupMenu replaceMenu;
	for (const auto& desc : types)
	{
		const juce::String pluginName = desc.name;
		replaceMenu.addItem(pluginName, [this, row, pluginName]()
			{
				juce::MessageManager::callAsync([this, row, pluginName]()
					{
						applyPluginReplacement(row, pluginName);
					});
			});
	}

	replaceMenu.showAt(anchor);
}

void MainComponent::applyPluginReplacement(int row, const juce::String& pluginName)
{
	if (row < 0 || row >= static_cast<int>(conductor.orchestra.size()) || pluginName.isEmpty())
		return;

	auto pluginId = conductor.orchestra[row].pluginInstanceId;

	pluginManager.resetPlugin(pluginId);
	pluginManager.instantiatePluginByName(pluginName, pluginId);

	for (auto& inst : conductor.orchestra)
	{
		if (inst.pluginInstanceId == pluginId)
			inst.pluginName = pluginName;
	}

	orchestraTable.updateContent();
	conductor.syncOrchestraWithPluginManager();
}

void MainComponent::scanForPlugins(PluginScanMode mode)
{
	if (!getFolder())
		return;

	auto pluginFolderName = pluginFolder.getFullPathName();
	if (pluginFolderName.isEmpty())
		return;

	juce::FileSearchPath searchPaths(pluginFolderName);
	const bool replaceExisting = (mode == PluginScanMode::Replace);

	pluginManager.scanPlugins(searchPaths, replaceExisting);

	DBG("Scanning completed.");

	// Update the plugins list in the ComboBox
	initPluginsList();
}

void MainComponent::initPlugins()
{
	addAndMakeVisible(pluginBox);
	pluginBox.addListener(this);
	pluginBox.setTextWhenNothingSelected("Instantiate Plugin");

	initPluginsList();
}

// load and update plugins list
void MainComponent::initPluginsList()
{
	// Try to load plugins first
	if (!pluginManager.loadPluginListFromFile())
	{
		scanForPlugins();
	}
	// Update the ListBox to display the plugins
	pluginBox.clear();
	const auto types = pluginManager.knownPluginList.getTypes();
	const int totalTypes = static_cast<int>(types.size());
	for (int i = 0; i < totalTypes; ++i)
	{
		pluginBox.addItem(types[i].name, i + 1);
	}
}

void MainComponent::initMidiInputs()
{
	addAndMakeVisible(midiInputList);
	midiInputList.setBounds(170, 300, 150, 30);
	midiInputList.addListener(this);

	refreshMidiInputs();
}

void MainComponent::refreshMidiInputs()
{
	auto midiInputs = juce::MidiInput::getAvailableDevices();
	DBG(midiInputs.size() << " MIDI Input Devices Available");
	// Clear the ComboBox
	midiInputList.clear();
	// Add the MIDI input devices to the ComboBox
	int id = 1;
	for (auto input : midiInputs)
	{
		midiInputList.addItem(input.name, id);
		id++;
	}
	// Add a refresh option
	midiInputList.addItem("Refresh List", -1);
	// Check we have some MIDI inputs
	if (midiInputs.size() == 0)
	{
		midiInputList.setText("No MIDI Inputs Available");
		return;
	}
	midiInputList.setSelectedId(1);
	// get the name of the first MIDI input
	juce::String midiInputName = midiInputs[0].name;
	midiManager.openMidiInput(midiInputName);
}

void MainComponent::initAudioDrivers()
{
	addAndMakeVisible(audioDriverList);
	audioDriverList.addListener(this);
	audioDriverList.setTextWhenNothingSelected("Select Driver");

	auto &deviceManager = pluginManager.getDeviceManager();
	auto &availableDeviceTypes = deviceManager.getAvailableDeviceTypes();

	audioDriverList.clear(juce::dontSendNotification);

	int itemId = 1;

	for (auto *type : availableDeviceTypes)
	{
		if (type == nullptr)
			continue;

		auto typeName = type->getTypeName();
		if (typeName.isNotEmpty())
			audioDriverList.addItem(typeName, itemId++);
	}

	if (audioDriverList.getNumItems() == 0)
	{
		audioDriverList.setEnabled(false);
		audioDriverList.setText("No Drivers Available", juce::dontSendNotification);
		return;
	}

	audioDriverList.setEnabled(true);

	auto currentType = deviceManager.getCurrentAudioDeviceType();
	bool matchedCurrent = false;
	if (currentType.isNotEmpty())
	{
		for (int i = 0; i < audioDriverList.getNumItems(); ++i)
		{
			if (audioDriverList.getItemText(i) == currentType)
			{
				audioDriverList.setSelectedId(i + 1, juce::dontSendNotification);
				matchedCurrent = true;
				break;
			}
		}
	}

	if (!matchedCurrent)
	{
		auto firstTypeName = audioDriverList.getItemText(0);
		audioDriverList.setSelectedId(1, juce::dontSendNotification);
		deviceManager.setCurrentAudioDeviceType(firstTypeName, true);
	}

	// Update the audio device list for the currently selected driver type
	updateAudioDeviceList();
}

void MainComponent::updateAudioDeviceList()
{
	auto &deviceManager = pluginManager.getDeviceManager();
	auto currentType = deviceManager.getCurrentAudioDeviceType();

	audioDeviceList.clear(juce::dontSendNotification);

	if (currentType.isEmpty())
	{
		audioDeviceLabel.setVisible(false);
		audioDeviceList.setVisible(false);
		return;
	}

	// Find the device type that matches the currently selected driver
	juce::AudioIODeviceType *selectedType = nullptr;
	for (auto *type : deviceManager.getAvailableDeviceTypes())
	{
		if (type != nullptr && type->getTypeName() == currentType)
		{
			selectedType = type;
			break;
		}
	}

	if (selectedType == nullptr)
	{
		audioDeviceLabel.setVisible(false);
		audioDeviceList.setVisible(false);
		return;
	}

	selectedType->scanForDevices();

	auto deviceNames = selectedType->getDeviceNames();
	int id = 1;
	for (auto &name : deviceNames)
	{
		audioDeviceList.addItem(name, id++);
	}

	if (audioDeviceList.getNumItems() == 0)
	{
		audioDeviceLabel.setVisible(false);
		audioDeviceList.setVisible(false);
		return;
	}

	audioDeviceLabel.setText(currentType + " Device", juce::dontSendNotification);
	audioDeviceLabel.setVisible(true);
	audioDeviceList.setVisible(true);
	audioDeviceList.setEnabled(true);

	// Select the currently active device if possible
	if (auto *currentDevice = deviceManager.getCurrentAudioDevice())
	{
		auto currentDeviceName = currentDevice->getName();
		for (int i = 0; i < audioDeviceList.getNumItems(); ++i)
		{
			if (audioDeviceList.getItemText(i) == currentDeviceName)
			{
				audioDeviceList.setSelectedId(i + 1, juce::dontSendNotification);
				return;
			}
		}
	}

	// Fall back to the first device when no current device is active or matches
	audioDeviceList.setSelectedId(1, juce::dontSendNotification);
}
void MainComponent::setSelectedAudioDriver(const juce::String &driverName)
{
	auto &deviceManager = pluginManager.getDeviceManager();
	// Set the driver if it's different from the current one
	if (driverName != deviceManager.getCurrentAudioDeviceType())
	{
		deviceManager.setCurrentAudioDeviceType(driverName, true);
	}
	// Update audio device list visibility and contents
	updateAudioDeviceList();
	// Ensure the UI reflects the actual current driver
	auto selectedDriver = deviceManager.getCurrentAudioDeviceType();
	if (selectedDriver.isNotEmpty())
	{
		for (int i = 0; i < audioDriverList.getNumItems(); ++i)
		{
			if (audioDriverList.getItemText(i) == selectedDriver)
			{
				audioDriverList.setSelectedId(i + 1, juce::dontSendNotification);
				break;
			}
		}
	}
}

void MainComponent::setSelectedAudioDevice(const juce::String &deviceName)
{
	auto &deviceManager = pluginManager.getDeviceManager();
	// Set the device if it's different from the current one
	if (auto *currentDevice = deviceManager.getCurrentAudioDevice())
	{
		if (deviceName != currentDevice->getName())
		{
			juce::AudioDeviceManager::AudioDeviceSetup setup;
			deviceManager.getAudioDeviceSetup(setup);
			setup.outputDeviceName = deviceName;
			setup.inputDeviceName = deviceName;
			auto result = deviceManager.setAudioDeviceSetup(setup, true);
			if (result.indexOfAnyOf("error", 0, true) >= 0)
			{
				juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
													   "Audio Device Error",
													   "Could not open audio device: " + deviceName);
			}
		}
	}
}

void MainComponent::comboBoxChanged(juce::ComboBox *comboBoxThatHasChanged)
{
	if (comboBoxThatHasChanged == &pluginBox)
	{
		int index = pluginBox.getSelectedId() - 1;
		const auto types = pluginManager.knownPluginList.getTypes();
		if (index >= 0 && index < static_cast<int>(types.size()))
		{
			const auto& desc = types[index];

			// Get selected row in the orchestra table
			auto selectedRows = orchestraTable.getSelectedRows();
			bool haveAdded = false;

			for (int i = 0; i < selectedRows.size(); ++i)
			{
				int row = selectedRows[i];
				// Get the instrument from the orchestra
				auto& instrument = conductor.orchestra[row];
				// Check if the pluginInstanceId for this instrument is already in pluginInstances, and if so, ignore
				if (pluginManager.hasPluginInstance(instrument.pluginInstanceId))
				{
					DBG("Plugin Instance ID already exists: " + instrument.pluginInstanceId);
					continue;
				}

				// Set the plugin name and plugin instance ID
				instrument.pluginName = desc.name;
				// call plugin update
				haveAdded = true;
				orchestraTable.updateContent();
			}
			// If no rows were selected or no valid rows, add a new instrument with the selected plugin
			if (!haveAdded)
			{
				addNewInstrument();
				auto& instrument = conductor.orchestra.back();
				instrument.pluginName = desc.name;
				orchestraTable.updateContent();
			}
			orchestraTable.repaint();
			conductor.syncOrchestraWithPluginManager();

			// Finally, deselect the pluginbox entry
			pluginBox.setSelectedId(0, juce::dontSendNotification);
		}
	}
	else if (comboBoxThatHasChanged == &midiInputList)
	{
		// Handle MIDI input selection
		juce::String midiInputName = midiInputList.getText();
		DBG("MIDI Input Selected: " + midiInputName);
		if (midiInputName == "Refresh List")
		{
			refreshMidiInputs();
			return;
		}
		midiManager.openMidiInput(midiInputName);
	}
	else if (comboBoxThatHasChanged == &audioDriverList)
	{
		auto selectedDriver = audioDriverList.getText();
		if (selectedDriver.isNotEmpty())
		{
			auto &deviceManager = pluginManager.getDeviceManager();
			if (selectedDriver != deviceManager.getCurrentAudioDeviceType())
			{
				auto previousType = deviceManager.getCurrentAudioDeviceType();
				deviceManager.setCurrentAudioDeviceType(selectedDriver, true);

				// If the device manager couldn't switch, revert UI selection
				auto appliedType = deviceManager.getCurrentAudioDeviceType();
				if (appliedType != selectedDriver)
				{
					auto setSelectionTo = [this](const juce::String &type)
					{
						if (type.isEmpty())
							return;

						for (int i = 0; i < audioDriverList.getNumItems(); ++i)
						{
							if (audioDriverList.getItemText(i) == type)
							{
								audioDriverList.setSelectedId(i + 1, juce::dontSendNotification);
								break;
							}
						}
					};

					if (appliedType.isNotEmpty())
						setSelectionTo(appliedType);
					else if (!previousType.isEmpty())
						setSelectionTo(previousType);
				}
			}
			// Update audio device list visibility and contents
			updateAudioDeviceList();
		}
	}
	else if (comboBoxThatHasChanged == &audioDeviceList)
	{
		auto selectedDevice = audioDeviceList.getText();
		if (selectedDevice.isNotEmpty())
		{
			auto &deviceManager = pluginManager.getDeviceManager();
			juce::AudioDeviceManager::AudioDeviceSetup setup;
			deviceManager.getAudioDeviceSetup(setup);
			setup.outputDeviceName = selectedDevice;
			setup.inputDeviceName = selectedDevice;
			auto result = deviceManager.setAudioDeviceSetup(setup, true);

			// If failed, show error and revert selection
			if (result.indexOfAnyOf("error", 0, true) >= 0)
			{
				juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
													   "Audio Device Error",
													   "Could not open audio device: " + selectedDevice);

				// Revert to previous device if possible
				auto *currentDevice = deviceManager.getCurrentAudioDevice();
				if (currentDevice)
				{
					auto currentDeviceName = currentDevice->getName();
					for (int i = 0; i < audioDeviceList.getNumItems(); ++i)
					{
						if (audioDeviceList.getItemText(i) == currentDeviceName)
						{
							audioDeviceList.setSelectedId(i + 1, juce::dontSendNotification);
							break;
						}
					}
				}
			}
		}
	}
}

// Set Midi input
void MainComponent::setMidiInput(juce::String inputText)
{
	auto midiInputs = juce::MidiInput::getAvailableDevices();
	for (auto input : midiInputs)
	{
		if (input.name == inputText)
		{
			midiManager.openMidiInput(input.name);
			// Set UI
			midiInputList.setText(input.name, juce::dontSendNotification);
			return;
		}
	}
}

// Helper to load settings from config.ini
void MainComponent::loadConfig()
{
	if (!configFile.existsAsFile())
		return;

	juce::StringArray lines;
	configFile.readLines(lines);

	for (auto &line : lines)
	{
		if (line.startsWith("bpm="))
			bpmEditor.setText(line.fromFirstOccurrenceOf("bpm=", false, false));
		else if (line.startsWith("audioStreamingPort="))
			audioStreamingPortEditor.setText(line.fromFirstOccurrenceOf("audioStreamingPort=", false, false));
		else if (line.startsWith("audioDriver="))
			setSelectedAudioDriver(line.fromFirstOccurrenceOf("audioDriver=", false, false));
		else if (line.startsWith("audioDevice="))
			setSelectedAudioDevice(line.fromFirstOccurrenceOf("audioDevice=", false, false));
		else if (line.startsWith("midiInput="))
			setMidiInput(line.fromFirstOccurrenceOf("midiInput=", false, false));
		// Add more settings as needed
	}
}

// Helper to save settings to config.ini
void MainComponent::saveConfig()
{
	juce::StringArray lines;
	lines.add("bpm=" + bpmEditor.getText());
	lines.add("audioStreamingPort=" + audioStreamingPortEditor.getText());
	lines.add("audioDriver=" + audioDriverList.getText());
	lines.add("audioDevice=" + audioDeviceList.getText());
	lines.add("midiInput=" + midiInputList.getText());
	// Add more settings as needed

	juce::String configText = lines.joinIntoString("\n");
	configFile.replaceWithText(configText);
}

// Helper to find next available tag number
int MainComponent::getNextTagNumber() const
{
	int maxTagNum = 0;
	for (const auto &inst : conductor.orchestra)
	{
		for (const auto &tag : inst.tags)
		{
			// Match "Tag {n}" pattern
			juce::String prefix = "Tag ";
			if (tag.startsWith(prefix))
			{
				juce::String numStr = tag.fromFirstOccurrenceOf(prefix, false, false).trim();
				int n = numStr.getIntValue();
				if (n > maxTagNum)
					maxTagNum = n;
			}
		}
	}
	return maxTagNum + 1;
}

// Helper to find next available pluginInstanceId number
int MainComponent::getNextInstanceNumber() const
{
	int maxInstanceNum = 0;
	juce::String prefix = "Instance ";
	for (const auto &inst : conductor.orchestra)
	{
		if (inst.pluginInstanceId.startsWith(prefix))
		{
			juce::String numStr = inst.pluginInstanceId.fromFirstOccurrenceOf(prefix, false, false).trim();
			int n = numStr.getIntValue();
			if (n > maxInstanceNum)
				maxInstanceNum = n;
		}
	}
	return maxInstanceNum + 1;
}

void MainComponent::removeMidiChannelFromOverdub(int midiChannel)
{
	midiManager.removeMidiChannelFromOverdub(midiChannel);
	midiManager.isStripped = false;
	updateOverdubUI();
}

MainComponent::LayoutMetrics MainComponent::getLayoutMetrics() const
{
	return LayoutMetrics{};
}

juce::Rectangle<float> MainComponent::computeTablePanelBounds(const LayoutMetrics &metrics, const juce::Rectangle<int> &tableBounds) const
{
	juce::ignoreUnused(metrics);
	const float panelInset = 6.0f;

	if (tableBounds.getWidth() <= 0 || tableBounds.getHeight() <= 0)
		return {};

	return juce::Rectangle<float>(
		tableBounds.getX() - panelInset,
		tableBounds.getY() - panelInset,
		tableBounds.getWidth() + panelInset * 2.0f,
		tableBounds.getHeight() + panelInset * 2.0f);
}

MainComponent::ButtonPanelLayout MainComponent::computeButtonPanelLayout(const LayoutMetrics &metrics, const juce::Rectangle<float> &tablePanelBounds) const
{
	ButtonPanelLayout layout;
	const float panelInset = 6.0f;
	const float cardSpacing = 18.0f;
	const int totalButtonHeight = metrics.numButtonRows * metrics.buttonHeight + (metrics.numButtonRows - 1) * metrics.spacingY;
	const int buttonAreaTop = getHeight() - totalButtonHeight - metrics.margin;

	float buttonPanelTop = static_cast<float>(buttonAreaTop) - panelInset;
	if (!tablePanelBounds.isEmpty())
		buttonPanelTop = juce::jmax(buttonPanelTop, tablePanelBounds.getBottom() + cardSpacing * 0.3f);

	const float buttonRowsTop = buttonPanelTop + panelInset;
	for (int i = 0; i < metrics.numButtonRows; ++i)
		layout.rowY[i] = static_cast<int>(buttonRowsTop + i * (metrics.buttonHeight + metrics.spacingY));

	const float buttonPanelBottom = buttonRowsTop + totalButtonHeight + panelInset;
	layout.panel = juce::Rectangle<float>(
		metrics.margin - panelInset,
		buttonPanelTop,
		(float)getWidth() - 2 * (metrics.margin - panelInset),
		juce::jmax(0.0f, buttonPanelBottom - buttonPanelTop));

	return layout;
}
