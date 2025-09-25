#include "MainComponent.h"


MainComponent::MainComponent()
{	
	//audioStreamer = std::make_unique<AudioUdpStreamer>("127.0.0.1", 10000);

	//pluginManager.audioTapCallback = [this](const juce::AudioBuffer<float>& buffer)
	//	{
	//		if (audioStreamer)
	//			audioStreamer->sendAudio(buffer);
	//	};

	setSize(600, 800);
	resized();
	juce::LookAndFeel::setDefaultLookAndFeel(&globalLNF);

	// Initialise the Orchestra TableListBox
	addAndMakeVisible(orchestraTableWrapper);
	initOrchestraTable();  // still needed
	addDataToTable();

	// Initialize the BPM editor
	addAndMakeVisible(bpmEditor);
	bpmEditor.setText("120"); // Default BPM
	bpmEditor.setJustification(juce::Justification::centred);
	bpmEditor.setInputRestrictions(5, "0123456789."); // Allow only numbers and a decimal point
	
	//// Initialise the audio streaming port editor
	//addAndMakeVisible(audioStreamingPortLabel);
	//audioStreamingPortLabel.setText("Audio Streaming Port", juce::dontSendNotification);
	//addAndMakeVisible(audioStreamingPortEditor);
	//audioStreamingPortEditor.setText("10000"); // Default port

	// Initialize the plugins
	initPlugins();
	initMidiInputs();

	// Initialize the "Scan" button
	addAndMakeVisible(ScanButton);
	ScanButton.onClick = [this]() { scanForPlugins(); }; // Use lambda for button click handling

	// Initialize the "Refresh" button
	addAndMakeVisible(updateButton);
	updateButton.onClick = [this]() { conductor.syncOrchestraWithPluginManager(); }; // Use lambda for button click handling

	// Initialize the "Get Recorded" button
	addAndMakeVisible(getRecordedButton);
	getRecordedButton.onClick = [this]() { midiManager.getRecorded(); updateOverdubUI(); }; // Use lambda for button click handling

	// Initialize the "List Plugin Instances" button
	addAndMakeVisible(listPluginInstancesButton);
	listPluginInstancesButton.onClick = [this]() { pluginManager.listPluginInstances(); }; // Use lambda for button click handling

	// Initialize the "Send Test Note" button
	addAndMakeVisible(sendTestNoteButton);
	sendTestNoteButton.onClick = [this]() { midiManager.sendTestNote(); }; // Use lambda for button click handling

	// Initialize the "Open Plugin" button
	addAndMakeVisible(openPluginButton);
	openPluginButton.onClick = [this]() { openPlugins(orchestraTable); }; // Use lambda for button click handling

	// Initialize the "Add Instrument" and "Remove Instrument" buttons
	addAndMakeVisible(addInstrumentButton);
	addInstrumentButton.onClick = [this]() { addInstrument(); }; // Use lambda for button click handling

	// Initialize Add New Instrument button
	addAndMakeVisible(addNewInstrumentButton);
	addNewInstrumentButton.onClick = [this]() { addNewInstrument(); }; // Use lambda for button click handling

	// Initialize the "Remove Instrument" button
	addAndMakeVisible(removeInstrumentButton);
	removeInstrumentButton.onClick = [this]() { removeInstrument(); }; // Use lambda for button click handling

	// Initialize the "Save" and "Restore" buttons
	addAndMakeVisible(saveButton);
	saveButton.onClick = [this]() { saveProject(); }; // Use lambda for button click handling

	addAndMakeVisible(restoreButton);
	restoreButton.onClick = [this]() { restoreProject(); }; // Use lambda for button click handling

	// Add recording buttons
	addAndMakeVisible(startRecordingButton);
	startRecordingButton.onClick = [this]() { midiManager.startRecording(); updateOverdubUI(); }; // Use lambda for button click handling

	// Add Project name label
	addAndMakeVisible(projectNameLabel);

	// Move selected rows to end of orchestra
	addAndMakeVisible(moveToEndButton);
	moveToEndButton.onClick = [this]() { moveSelectedRowsToEnd(); }; // Use lambda for button click handling

	// Initialize the "Start Overdub" button
	addAndMakeVisible(startOverdubButton);
	startOverdubButton.onClick = [this]() { midiManager.startOverdub(); updateOverdubUI(); };

	// Initialize the "Stop Overdub" button
	addAndMakeVisible(stopOverdubButton);
	stopOverdubButton.onClick = [this]() { midiManager.stopOverdub(); updateOverdubUI(); };

	// Initialize the "Strip Silence" button
        addAndMakeVisible(stripLeadingSilenceButton);
        stripLeadingSilenceButton.onClick = [this]() { midiManager.stripLeadingSilence(); updateOverdubUI(); };

        // Initialize the "Undo Overdub" button
        addAndMakeVisible(undoOverdubButton);
        undoOverdubButton.onClick = [this]() { midiManager.undoLastOverdub(); updateOverdubUI(); };

        addAndMakeVisible(importMidiButton);
        importMidiButton.onClick = [this]() { midiManager.importMidiFileToRecordBuffer(); updateOverdubUI(); };

        addAndMakeVisible(exportMidiButton);
        exportMidiButton.onClick = [this]() { midiManager.exportRecordBufferToMidiFile(); };

        updateOverdubUI();
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

	bpmEditor.setJustification(juce::Justification::centred);
	audioStreamingPortEditor.setJustification(juce::Justification::centred);

	// --- Reserved space at bottom for buttons ---
	const int totalButtonHeight = numButtonRows * buttonHeight + (numButtonRows - 1) * spacingY;
	const int buttonAreaTop = windowHeight - totalButtonHeight - margin;

	// --- Table area: from bottom of top controls to top of button area ---
	const int tableTop = projectNameLabel.getBottom() + spacingY;
	const int tableHeight = buttonAreaTop - tableTop - spacingY;  // extra spacing between table and buttons
	orchestraTableWrapper.setBounds(margin, tableTop, windowWidth - 2 * margin, tableHeight);

	// --- Button rows, starting from bottom upward ---

	// Row 1 (bottom row)
	int row1Y = windowHeight - margin - buttonHeight;
	ScanButton.setBounds(margin, row1Y, buttonWidth, buttonHeight);
	updateButton.setBounds(ScanButton.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);
	getRecordedButton.setBounds(updateButton.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);
	sendTestNoteButton.setBounds(getRecordedButton.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);
	addInstrumentButton.setBounds(sendTestNoteButton.getRight() + spacingX, row1Y, buttonWidth, buttonHeight);

	// Row 2
	int row2Y = row1Y - buttonHeight - spacingY;
	pluginBox.setBounds(margin, row2Y, buttonWidth, buttonHeight);
	midiInputList.setBounds(pluginBox.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	listPluginInstancesButton.setBounds(midiInputList.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	openPluginButton.setBounds(listPluginInstancesButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);
	removeInstrumentButton.setBounds(openPluginButton.getRight() + spacingX, row2Y, buttonWidth, buttonHeight);

	// Row 3 (top row of buttons)
	int row3Y = row2Y - buttonHeight - spacingY;
	saveButton.setBounds(margin, row3Y, buttonWidth, buttonHeight);
	restoreButton.setBounds(saveButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	startRecordingButton.setBounds(restoreButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	addNewInstrumentButton.setBounds(startRecordingButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);
	moveToEndButton.setBounds(addNewInstrumentButton.getRight() + spacingX, row3Y, buttonWidth, buttonHeight);

	// Row 4
	int row4Y = row3Y - buttonHeight - spacingY;
        startOverdubButton.setBounds(margin, row4Y, buttonWidth, buttonHeight);
        stopOverdubButton.setBounds(startOverdubButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);
        stripLeadingSilenceButton.setBounds(stopOverdubButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);
        undoOverdubButton.setBounds(stripLeadingSilenceButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);
        importMidiButton.setBounds(undoOverdubButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);
        exportMidiButton.setBounds(importMidiButton.getRight() + spacingX, row4Y, buttonWidth, buttonHeight);



	// --- BPM Sync handler ---
	bpmEditor.onTextChange = [this]() {
		pluginManager.setBpm(getBpm());
		};

	// --- Audio Streaming Port handler ---
	audioStreamingPortEditor.onFocusLost = [this]() {
		handleAudioPortChange();
		};

	audioStreamingPortEditor.onReturnKey = [this]() {
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


MainComponent::~MainComponent()
{
	pluginManager.releaseResources();
	midiManager.closeMidiInput();
	conductor.shutdown(); // <--- important for clean exit
	juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
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

void MainComponent::saveProject(const std::vector<InstrumentInfo>& selectedInstruments)
{
	// Get the full file paths from the user's documents directory
	juce::File dataFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("projectData.dat");
	juce::File pluginsFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("projectPlugins.dat");
	juce::File metaFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("projectMeta.xml");

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
			zipBuilder.addFile(dataFile, 5, "projectData.dat");  // Using moderate compression
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
	if (fileChooser.browseForFileToOpen())
	{
		juce::File zipFile = fileChooser.getResult();
		DBG("Selected Project: " + zipFile.getFullPathName());
		juce::FileInputStream inputStream(zipFile);
		if (inputStream.openedOk())
		{
			DBG("Reading Project...");
			juce::ZipFile zip(inputStream);
			DBG("Project Read.");

			// Define the extraction locations
			juce::File dataFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("projectData.dat");
			juce::File pluginsFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("projectPlugins.dat");
			juce::File metaFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("projectMeta.xml");

			// Extract each file
			auto extractFile = [&](const juce::String& fileName, const juce::File& destination)
				{
					auto index = zip.getIndexOfFileName(fileName);
					if (index >= 0)
					{
						auto* fileStream = zip.createStreamForEntry(index);
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
			orchestraTable.updateContent();

			// Get the restored project name
			juce::String projectName = zipFile.getFileNameWithoutExtension();
			DBG("Project Restored: " + projectName);
			updateProjectNameLabel(projectName);
		}
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

void MainComponent::openPlugins(juce::TableListBox& table)
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
	int newRow = -1;  // We'll use this to keep track of the new instrument's row index

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
			auto& previousInstrument = conductor.orchestra.back();
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
	int newRow = -1;  // We'll use this to keep track of the new instrument's row index
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

void MainComponent::basicInstrument(InstrumentInfo& instrument)
{
	instrument.instrumentName = "New Instrument";
	instrument.pluginName = "New Plugin";
	instrument.pluginInstanceId = "New Instance";
	instrument.midiChannel = 1;
	instrument.tags.push_back("tag1");
	instrument.tags.push_back("tag2");
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


void MainComponent::paint(juce::Graphics& g)
{
	g.setColour(juce::Colours::lightgrey.darker(0.8f));  // near-black but not pure
	g.fillAll();
}


double MainComponent::getBpm() const
{
	return bpmEditor.getText().getDoubleValue();  // Or however you're storing user input
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
}

void MainComponent::initPlugins()
{
	addAndMakeVisible(pluginBox);
	pluginBox.addListener(this);
	pluginBox.setTextWhenNothingSelected("Select a Plugin");

	// Try to load plugins first
	if (!pluginManager.loadPluginListFromFile())
	{
		scanForPlugins();
	}
    
	//Update the ListBox to display the plugins
	pluginBox.clear();
	for (int i = 0; i < pluginManager.knownPluginList.getNumTypes(); ++i)
	{
		juce::PluginDescription* desc = pluginManager.knownPluginList.getType(i);
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

	// Get the list of MIDI input devices
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

	// Set the first item as the default
	if (midiInputs.size() > 0)
	{
		midiInputList.setSelectedId(1);
		// get the name of the first MIDI input
		juce::String midiInputName = midiInputs[0].name;
		midiManager.openMidiInput(midiInputName);
	}
	else
	{
		midiInputList.setText("No MIDI Inputs Available");
	}
}

void MainComponent::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
	if (comboBoxThatHasChanged == &pluginBox)
	{
		int index = pluginBox.getSelectedId() - 1;
		if (index >= 0)
		{
			juce::PluginDescription* desc = pluginManager.knownPluginList.getType(index);
			if (desc != nullptr)
			{
				//pluginManager.instantiateSelectedPlugin(desc);
				// add '_all' tag to the plugin as a vector
				//std::vector<juce::String> tags;
				//tags.push_back("_all");

				// Get selected row in the orchestra table
				auto selectedRows = orchestraTable.getSelectedRows();
				for (int i = 0; i < selectedRows.size(); ++i)
				{
					int row = selectedRows[i];
					// Get the instrument from the orchestra
					auto& instrument = conductor.orchestra[row];
					// Set the plugin name and plugin instance ID
					instrument.pluginName = desc->name;
					orchestraTable.updateContent();

					// Finally, deselect the pluginbox entry
					pluginBox.setSelectedId(0, juce::dontSendNotification);
				}


			}
		}
	}
	else if (comboBoxThatHasChanged == &midiInputList)
	{
		// Handle MIDI input selection
		juce::String midiInputName = midiInputList.getText();
		DBG("MIDI Input Selected: " + midiInputName);
		midiManager.openMidiInput(midiInputName);

	}
}
