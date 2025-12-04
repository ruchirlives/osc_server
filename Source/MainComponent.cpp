#include "MainComponent.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

static bool isRunningInDebugger()
{
#if JUCE_WINDOWS
	return ::IsDebuggerPresent();
#elif JUCE_MAC || JUCE_LINUX
	return juce::SystemStats::getEnvironmentVariable("JUCE_DEBUGGER", "").isNotEmpty();
#else
	return false;
#endif
}

MainComponent::MainComponent()
{

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
	addAndMakeVisible(bpmEditor);
	bpmEditor.setText("120"); // Default BPM
	bpmEditor.setJustification(juce::Justification::centred);
	bpmEditor.setInputRestrictions(5, "0123456789."); // Allow only numbers and a decimal point

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
	{ scanForPlugins(); }; // Use lambda for button click handling

	// Initialize the "Refresh" button
	addAndMakeVisible(updateButton);
	updateButton.onClick = [this]()
	{ conductor.syncOrchestraWithPluginManager(); }; // Use lambda for button click handling

	// Initialize the "Get Recorded" button
	addAndMakeVisible(getRecordedButton);
	getRecordedButton.onClick = [this]()
	{ midiManager.getRecorded(); updateOverdubUI(); }; // Use lambda for button click handling

	// Initialize the "List Plugin Instances" button
	addAndMakeVisible(listPluginInstancesButton);
	listPluginInstancesButton.onClick = [this]()
	{ pluginManager.listPluginInstances(); }; // Use lambda for button click handling

	// Initialize the "Send Test Note" button
	addAndMakeVisible(sendTestNoteButton);
	sendTestNoteButton.onClick = [this]()
	{ midiManager.sendTestNote(); }; // Use lambda for button click handling

	// Initialize the "Open Plugin" button
	addAndMakeVisible(openPluginButton);
	openPluginButton.onClick = [this]()
	{ openPlugins(orchestraTable); }; // Use lambda for button click handling

	// Initialize the "Add Instrument" and "Remove Instrument" buttons
	addAndMakeVisible(addInstrumentButton);
	addInstrumentButton.onClick = [this]()
	{ addInstrument(); }; // Use lambda for button click handling

	// Initialize Add New Instrument button
	addAndMakeVisible(addNewInstrumentButton);
	addNewInstrumentButton.onClick = [this]()
	{ addNewInstrument(); }; // Use lambda for button click handling

	// Initialize the "Remove Instrument" button
	addAndMakeVisible(removeInstrumentButton);
	removeInstrumentButton.onClick = [this]()
	{ removeInstrument(); }; // Use lambda for button click handling

	// Initialize the "Save" and "Restore" buttons
	addAndMakeVisible(saveButton);
	saveButton.onClick = [this]()
	{ saveProject(); }; // Use lambda for button click handling

	addAndMakeVisible(restoreButton);
	restoreButton.onClick = [this]()
	{ restoreProject(); }; // Use lambda for button click handling

	// Add Project name label
	addAndMakeVisible(projectNameLabel);

	// Move selected rows to end of orchestra
	addAndMakeVisible(moveToEndButton);
	moveToEndButton.onClick = [this]()
	{ moveSelectedRowsToEnd(); }; // Use lambda for button click handling

	// Initialize the "Start Overdub" button
	addAndMakeVisible(startOverdubButton);
	startOverdubButton.onClick = [this]()
	{ midiManager.startOverdub(); updateOverdubUI(); };

	// Initialize the "Stop Overdub" button
	addAndMakeVisible(stopOverdubButton);
	stopOverdubButton.onClick = [this]()
	{ midiManager.stopOverdub(); updateOverdubUI(); };

	// Initialize the "Play Overdub" button
	addAndMakeVisible(playOverdubButton);
	playOverdubButton.onClick = [this]()
	{ midiManager.playOverdub(); updateOverdubUI(); };

	// playOverdubOnTriggerButton
	addAndMakeVisible(triggerOverdubButton);
	triggerOverdubButton.onClick = [this]()
	{ midiManager.triggerOverdub(); updateOverdubUI(); };

	// Initialize the "Strip Silence" button
	addAndMakeVisible(stripLeadingSilenceButton);
	stripLeadingSilenceButton.onClick = [this]()
	{ midiManager.stripLeadingSilence(); updateOverdubUI(); };

	// Initialize the "Undo Overdub" button
	addAndMakeVisible(undoOverdubButton);
	undoOverdubButton.onClick = [this]()
	{ midiManager.undoLastOverdub(); updateOverdubUI(); };

	addAndMakeVisible(importMidiButton);
	importMidiButton.onClick = [this]()
	{ midiManager.importMidiFileToRecordBuffer(); updateOverdubUI(); };

	addAndMakeVisible(exportMidiButton);
	exportMidiButton.onClick = [this]()
	{ midiManager.exportRecordBufferToMidiFile(); };

	// Set up config file path
	juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DawServer");
	if (!dawServerDir.exists())
		dawServerDir.createDirectory();
	configFile = dawServerDir.getChildFile("config.ini");

	loadConfig();

	resized();
	updateOverdubUI();
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
	const int margin = 10;
	const int buttonWidth = 150;
	const int buttonHeight = 30;
	const int spacingX = 10;
	const int spacingY = 20;
	const int labelHeight = 30;
	const int numButtonRows = 4;

	const int windowWidth = getWidth();
	const int windowHeight = getHeight();

	// Set main window position (if needed)
	getTopLevelComponent()->setTopLeftPosition(1600, 100);

	// --- Top controls ---
	projectNameLabel.setBounds(margin, margin, buttonWidth, labelHeight);
	bpmEditor.setBounds(projectNameLabel.getRight() + spacingX, margin, 75, labelHeight);

	audioStreamingPortLabel.setBounds(bpmEditor.getRight() + spacingX, margin, 150, labelHeight);
	audioStreamingPortEditor.setBounds(audioStreamingPortLabel.getRight() + spacingX, margin, 75, labelHeight);

	const int driverRowY = audioStreamingPortEditor.getBottom() + spacingY / 2;
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

	// --- Button rows, starting from bottom upward ---

	// Row 1 (bottom row) - Scan, Select Plugin, Update, Open Plugin, List Plugin Instances
	int row1Y = windowHeight - margin - buttonHeight;
	ScanButton.setBounds(margin, row1Y, buttonWidth, buttonHeight);
	pluginBox.setBounds(ScanButton.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);
	updateButton.setBounds(pluginBox.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);
	openPluginButton.setBounds(updateButton.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);
	listPluginInstancesButton.setBounds(openPluginButton.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);

	// Row 2 - Instrument management and recording
	int row2Y = row1Y - buttonHeight - spacingY;
	addInstrumentButton.setBounds(margin, row2Y, buttonWidth, buttonHeight);
	addNewInstrumentButton.setBounds(addInstrumentButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	removeInstrumentButton.setBounds(addNewInstrumentButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	moveToEndButton.setBounds(removeInstrumentButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	getRecordedButton.setBounds(moveToEndButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);

	// Row 3 - MIDI and utility controls
	int row3Y = row2Y - buttonHeight - spacingY;
	sendTestNoteButton.setBounds(margin, row3Y, buttonWidth, buttonHeight);
	importMidiButton.setBounds(sendTestNoteButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	exportMidiButton.setBounds(importMidiButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	midiInputList.setBounds(exportMidiButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	stripLeadingSilenceButton.setBounds(midiInputList.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);

	// Row 4 (top row of buttons) - Project and overdub controls
	int row4Y = row3Y - buttonHeight - spacingY;
	saveButton.setBounds(margin, row4Y, buttonWidth, buttonHeight);
	restoreButton.setBounds(saveButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);
	startOverdubButton.setBounds(restoreButton.getRight() + spacingX, row4Y, buttonWidth / 3, buttonHeight);
	triggerOverdubButton.setBounds(startOverdubButton.getRight() + spacingX, row4Y, buttonWidth / 3, buttonHeight);
	playOverdubButton.setBounds(triggerOverdubButton.getRight() + spacingX, row4Y, buttonWidth / 3, buttonHeight);
	stopOverdubButton.setBounds(playOverdubButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);
	undoOverdubButton.setBounds(stopOverdubButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);

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
	projectNameLabel.setText("Project Name: " + projectName, juce::dontSendNotification);
}

void MainComponent::saveProject(const std::vector<InstrumentInfo> &selectedInstruments)
{
	// Create DawServer subfolder in user's documents directory
	juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DawServer");
	if (!dawServerDir.exists())
		dawServerDir.createDirectory();

	// Get the full file paths in DawServer subfolder
	juce::File dataFile = dawServerDir.getChildFile("projectData.dat");
	juce::File pluginsFile = dawServerDir.getChildFile("projectPlugins.dat");
	juce::File metaFile = dawServerDir.getChildFile("projectMeta.xml");

	// Save the project state files
	conductor.saveAllData(dataFile.getFullPathName(), pluginsFile.getFullPathName(), metaFile.getFullPathName(), selectedInstruments);

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
	// Open a file chooser dialog to select the zip file
	juce::FileChooser fileChooser("Open Project", juce::File(), "*.oscdaw");
	projectNameLabel.setText("Project Name: Restoring...", juce::dontSendNotification);
	projectNameLabel.repaint();
	if (fileChooser.browseForFileToOpen())
	{
		const juce::String previousLabelText = projectNameLabel.getText();
		bool restoreSucceeded = false;

		juce::File zipFile = fileChooser.getResult();
		DBG("Selected Project: " + zipFile.getFullPathName());
		juce::FileInputStream inputStream(zipFile);
		if (inputStream.openedOk())
		{
			DBG("Reading Project...");
			juce::ZipFile zip(inputStream);
			DBG("Project Read.");

			// Create DawServer subfolder in user's documents directory
			juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DawServer");
			if (!dawServerDir.exists())
				dawServerDir.createDirectory();

			// Define the extraction locations in DawServer subfolder
			juce::File dataFile = dawServerDir.getChildFile("projectData.dat");
			juce::File pluginsFile = dawServerDir.getChildFile("projectPlugins.dat");
			juce::File metaFile = dawServerDir.getChildFile("projectMeta.xml");

			// Extract each file
			auto extractFile = [&](const juce::String &fileName, const juce::File &destination)
			{
				auto index = zip.getIndexOfFileName(fileName);
				if (index >= 0)
				{
					auto *fileStream = zip.createStreamForEntry(index);
					if (fileStream != nullptr)
					{
						if (destination.exists())
						{
							// Delete the existing file before overwriting
							destination.deleteFile();
						}
						juce::FileOutputStream outStream(destination);
						if (outStream.openedOk())
							outStream.writeFromInputStream(*fileStream, -1);

						delete fileStream;
					}
				}
			};

			DBG("Unzipping Project...");
			extractFile("projectData.dat", dataFile);
			extractFile("projectPlugins.dat", pluginsFile);
			extractFile("projectMeta.xml", metaFile);
			DBG("Project Unzipped.");

			if (!append)
			{
				// Restore project state
				conductor.restoreAllData(dataFile.getFullPathName(), pluginsFile.getFullPathName(), metaFile.getFullPathName());
			}
			else
			{
				// Append project state
				conductor.upsertAllData(dataFile.getFullPathName(), pluginsFile.getFullPathName(), metaFile.getFullPathName());
			}

			// Update the orchestra table
			refreshOrchestraTableUI();

			// Get the restored project name
			juce::String projectName = zipFile.getFileNameWithoutExtension();
			DBG("Project Restored: " + projectName);
			updateProjectNameLabel(projectName);
			repaint();
			restoreSucceeded = true;
		}

		if (!restoreSucceeded)
		{
			projectNameLabel.setText(previousLabelText, juce::dontSendNotification);
			repaint();
			if (auto *top = getTopLevelComponent())
			{
				auto w = top->getWidth();
				auto h = top->getHeight();
				top->setSize(w + 1, h);
				top->setSize(w, h);
			}
		}
	}
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
	instrument1.instrumentName = "My Soundcase Test Instrument";
	instrument1.pluginName = "Soundcase";
	instrument1.pluginInstanceId = "Selection 1";
	instrument1.midiChannel = 1;
	instrument1.tags.push_back("tag1");
	instrument1.tags.push_back("tag2");

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
			newRow = conductor.orchestra.size();
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
		newRow = conductor.orchestra.size() - 1;

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
	newRow = conductor.orchestra.size() - 1;
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

void MainComponent::getFolder()
{
	juce::FileChooser fileChooser("Select a directory with plugins");
	if (fileChooser.browseForDirectory())
	{
		DBG("Scanning for plugins VST3...");
		pluginFolder = fileChooser.getResult();
	}
	else
	{
		DBG("No folder selected");
	}
}

void MainComponent::paint(juce::Graphics &g)
{
	g.setColour(juce::Colours::lightgrey.darker(0.8f)); // near-black but not pure
	g.fillAll();
}

double MainComponent::getBpm() const
{
	return bpmEditor.getText().getDoubleValue(); // Or however you're storing user input
}

void MainComponent::scanForPlugins()
{
	// get folder
	getFolder();

	// Get the full path of the plugin folder
	auto pluginFolderName = pluginFolder.getFullPathName();

	// Scan for plugins in the specified directory
	auto searchPaths(pluginFolderName);

	pluginManager.scanPlugins(searchPaths);

	DBG("Scanning completed.");

	// Update the plugins list in the ComboBox
	initPluginsList();
}

void MainComponent::initPlugins()
{
	addAndMakeVisible(pluginBox);
	pluginBox.addListener(this);
	pluginBox.setTextWhenNothingSelected("Select a Plugin");

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
	for (int i = 0; i < pluginManager.knownPluginList.getNumTypes(); ++i)
	{
		juce::PluginDescription *desc = pluginManager.knownPluginList.getType(i);
		if (desc != nullptr)
		{
			pluginBox.addItem(desc->name, i + 1);
		}
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
		if (index >= 0)
		{
			juce::PluginDescription *desc = pluginManager.knownPluginList.getType(index);
			if (desc != nullptr)
			{

				// Get selected row in the orchestra table
				auto selectedRows = orchestraTable.getSelectedRows();
				bool haveAdded = false;

				for (int i = 0; i < selectedRows.size(); ++i)
				{
					int row = selectedRows[i];
					// Get the instrument from the orchestra
					auto &instrument = conductor.orchestra[row];
					// Check if the pluginInstanceId for this instrument is already in pluginInstances, and if so, ignore
					if (pluginManager.hasPluginInstance(instrument.pluginInstanceId))
					{
						DBG("Plugin Instance ID already exists: " + instrument.pluginInstanceId);
						continue;
					}

					// Set the plugin name and plugin instance ID
					instrument.pluginName = desc->name;
					// call plugin update
					haveAdded = true;
					orchestraTable.updateContent();
				}
				// If no rows were selected or no valid rows, add a new instrument with the selected plugin
				if (!haveAdded)
				{
					addNewInstrument();
					auto &instrument = conductor.orchestra.back();
					instrument.pluginName = desc->name;
				}
				conductor.syncOrchestraWithPluginManager();

				// Finally, deselect the pluginbox entry
				pluginBox.setSelectedId(0, juce::dontSendNotification);
			}
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
