#include "Conductor.h"
#include "MainComponent.h"


// Constructor: takes a reference to PluginManager and passes it
Conductor::Conductor(PluginManager& pm, MidiManager& mm, MainComponent* mainComponentRef)
	: pluginManager(pm), midiManager(mm), mainComponent(mainComponentRef)
{
	// Add this instance as an OSC listener
	addListener(this, "/midi/message");
	addListener(this, "/orchestra");

	// initial sync of orchestra with PluginManager
	syncOrchestraWithPluginManager();
	initializeOSCReceiver(8000);
	initializeOSCSender("239.255.0.1", 9000);
}

// Destructor
Conductor::~Conductor()
{
	// Ensure to remove the listener and close the OSC receiver
	removeListener(this);
	OSCSender::disconnect();
	OSCReceiver::disconnect();
}

// Initialise OSC Sender with a specific host and port
void Conductor::initializeOSCSender(const juce::String& host, int port)
{
	if (!OSCSender::connect(host, port))
	{
		DBG("Error: Unable to connect to OSC host: " + host + " on port: " + juce::String(port));
	}
	else
	{
		DBG("OSC Sender connected to host: " + host + " on port: " + juce::String(port));
	}
}

void Conductor::sendOSCMessage(const std::vector<juce::String>& tags)
{
	juce::OSCMessage message("/selected/tags");
	for (const auto& tag : tags)
	{
		message.addString(tag);
	}
	OSCSender::send(message);
	DBG("Sent OSC message: " + message.getAddressPattern().toString());

	// Set lastTag to last tag sent
	lastTags = tags;
}

void Conductor::send_lastTag()
{
	sendOSCMessage(lastTags);
}

// Initialize OSC Receiver with a specific port
void Conductor::initializeOSCReceiver(int port)
{
	if (!OSCReceiver::connect(port))
	{
		DBG("Error: Unable to connect to OSC port: " + juce::String(port));
	}
	else
	{
		DBG("OSC Receiver connected on port: " + juce::String(port));
	}
}

//convert string array to vector
void Conductor::stringArrayToVector(juce::StringArray stringArray, std::vector<juce::String>& stringVector)
{
	for (int i = 0; i < stringArray.size(); i++)
	{
		stringVector.push_back(stringArray[i]);
	}
}

// Callback function for receiving OSC messages
void Conductor::oscMessageReceived(const juce::OSCMessage& message)
{
	// DBG print the message
	// DBG("Received OSC message: " + message.getAddressPattern().toString());

	// Ensure the message has at least the necessary components for MIDI data and tags
	if (message.size() > 0 && message[0].isString())
	{
		juce::String messageType = message[0].getString();
		juce::String messageAddress = message.getAddressPattern().toString();

		// Handle add_instrument command by checking the message address
		if (messageAddress == "/orchestra")
		{
			if (messageType == "add_instrument")
			{
				oscAddInstrumentCommand(message);
			}
			else if (messageType == "get_recorded")
			{
				// activate get_recorded method
				DBG("Received get_recorded command");
				 midiManager.getRecorded();
			}
			else if (messageType == "save_project")
			{
				saveAllData("projectData.dat", "projectPlugins.dat", "projectMeta.xml");
			}
			else if (messageType == "restore_project")
			{
				restoreAllData("projectData.dat", "projectPlugins.dat", "projectMeta.xml");
			}
			else if (messageType == "restore_from_file")
			{
				DBG("Received restore from file request for file: ");
				mainComponent->restoreProject(false); // false means do not append, just restore

			}
			else if (messageType == "request_tags")
			{
				DBG("Received request for tags");
				// Get the tags of the first currently selected instrument
				juce::String tags;
				if (!orchestra.empty())
				{
					tags = orchestra[0].tags.empty() ? "" : orchestra[0].tags[0];
				}
				DBG("Sending tags: " + tags);

				// Send the tags back to the sender
				send_lastTag();

			}

		}
		// Handle MIDI messages
		else if (messageAddress == "/midi/message")
		{
			oscProcessMIDIMessage(message);
		}
	}
}


void Conductor::oscAddInstrumentCommand(const juce::OSCMessage& message)
{
    if (message.size() >= 4 && message[0].isString() && message[1].isString() && message[2].isInt32())
    {
        juce::String instrumentName = message[0].getString();
        juce::String pluginInstanceId = message[1].getString();
        int midiChannel = message[2].getInt32();

		// Extract tags starting from index 3
        std::vector<juce::String> tags = extractTags(message, 3);

        // Check if an instrument with the same pluginInstanceId and midiChannel already exists
        for (auto& instrument : orchestra)
        {
            if (instrument.pluginInstanceId == pluginInstanceId && instrument.midiChannel == midiChannel)
            {
				// Is the pluginName the same?
				if (instrument.pluginName != message[1].getString())
				{
					DBG("Error: Plugin name does not match for existing instrument with pluginInstanceId: " + pluginInstanceId);
					// reset the pluginInstanceId
					pluginManager.resetPlugin(pluginInstanceId);
					// instantiate the plugin with the new plugin name
					pluginManager.instantiatePluginByName(message[1].getString(), pluginInstanceId);
				}

				// Update the existing entry with the new instrumentName
                instrument.instrumentName = instrumentName;
                instrument.tags = tags;
                DBG("Updated existing instrument in orchestra: " + instrumentName);
                syncOrchestraWithPluginManager();
                return;
            }
        }

        // Create a new InstrumentInfo struct and add it to the orchestra vector
		InstrumentInfo newInstrument;
        newInstrument.instrumentName = instrumentName;
        newInstrument.pluginInstanceId = pluginInstanceId;
        newInstrument.midiChannel = midiChannel;
        newInstrument.tags = tags;

        orchestra.push_back(newInstrument);

        // Sync the orchestra list with PluginManager
        syncOrchestraWithPluginManager();
    }
    else
    {
        DBG("Error: Incorrect OSC message format for adding instrument");
    }
}

std::vector<std::pair<juce::String, int>> Conductor::extractPluginIdsAndChannels(const juce::OSCMessage& message, int startIndex)
{
	std::vector<juce::String> tags = extractTags(message, startIndex);
	std::vector<std::pair<juce::String, int>> pluginIdsAndChannels;

	// Find the plugin IDs associated with the tags and store them with the MIDI channel
	for (const auto& tag : tags)
	{
		for (const auto& instrument : orchestra)
		{
			if (std::find(instrument.tags.begin(), instrument.tags.end(), tag) != instrument.tags.end())
			{
				// midiChannel is 0 based in OSC messages
				int midiChannel = instrument.midiChannel - 1;

				pluginIdsAndChannels.emplace_back(instrument.pluginInstanceId, midiChannel);
			}
		}
	}
	return pluginIdsAndChannels;
}

void Conductor::oscProcessMIDIMessage(const juce::OSCMessage& message)
{
	juce::String messageType = message[0].getString();
	if (messageType == "note_on" || messageType == "note_off")
	{
		if (messageType == "note_on")
		{
			int note = message[1].getInt32();
			int velocity = message[2].getInt32();
			juce::int64 timestamp = adjustTimestamp(message[3]);
			// DBG("Received note_on message: " << note << ", velocity: " << velocity << ", timestamp: " << juce::String(timestamp));

			// Extract pluginIds and channels using tags starting from index 4
			std::vector<std::pair<juce::String, int>> pluginIdsAndChannels = extractPluginIdsAndChannels(message, 4);

			// Send a message to each pluginId with its respective channel
			for (const auto& [pluginId, channel] : pluginIdsAndChannels)
			{
				handleIncomingNote(messageType, channel, note, velocity, pluginId, timestamp);
				DBG("Received note on for plugin: " + pluginId + " on channel: " + juce::String(channel) + " with note: " + juce::String(note) + " and velocity: " + juce::String(velocity) + " at time " + juce::String(timestamp));
			}
		}
		else if (messageType == "note_off")
		{
			int velocity = 0; // Note off velocity is 0
			int note = message[1].getInt32();
			juce::int64 timestamp = adjustTimestamp(message[2]);

			// Extract pluginIds and channels using tags starting from index 3
			std::vector<std::pair<juce::String, int>> pluginIdsAndChannels = extractPluginIdsAndChannels(message, 3);

			// Send a message to each pluginId with its respective channel
			for (const auto& [pluginId, channel] : pluginIdsAndChannels)
			{
				handleIncomingNote(messageType, channel, note, velocity, pluginId, timestamp);
			}
		}
		
	}
	else if (messageType == "controller")
	{
		// Handle control_change messages
		// Extract the control change parameters

		int controllerNumber = message[1].getInt32();
		int controllerValue = message[2].getInt32();
		juce::int64 timestamp = adjustTimestamp(message[3]);

		// Extract pluginIds and channels using tags starting from index 4
		std::vector<std::pair<juce::String, int>> pluginIdsAndChannels = extractPluginIdsAndChannels(message, 4);

		// Send a message to each pluginId with its respective channel
		for (const auto& [pluginId, channel] : pluginIdsAndChannels)
		{
			handleIncomingControlChange(channel, controllerNumber, controllerValue, pluginId, timestamp);
		}
	}
    else if (messageType == "pitchbend")
    {
        int pitchBendValue = message[1].getInt32();
        juce::int64 timestamp = adjustTimestamp(message[2]);
        
        std::vector<std::pair<juce::String, int>> pluginIdsAndChannels = extractPluginIdsAndChannels(message, 3);
        
        for (const auto& [pluginId, channel] : pluginIdsAndChannels)
        {
            handleIncomingPitchBend(channel, pitchBendValue, pluginId, timestamp);
            DBG("Received pitch bend for plugin: " + pluginId + " on channel: " + juce::String(channel) + " with value: " + juce::String(pitchBendValue) + " at time " + juce::String(timestamp));
        }
    }
	else if (messageType == "program_change")
	{
		int programNumber = message[1].getInt32();
		juce::int64 timestamp = adjustTimestamp(message[2]);
		// Extract pluginIds and channels using tags starting from index 3
		std::vector<std::pair<juce::String, int>> pluginIdsAndChannels = extractPluginIdsAndChannels(message, 3);
		for (const auto& [pluginId, channel] : pluginIdsAndChannels)
		{
			handleIncomingProgramChange(channel, programNumber, pluginId, timestamp);
			DBG("Received program change for plugin: " + pluginId + " on channel: " + juce::String(channel) + " to program: " + juce::String(programNumber));
		}

	}
	else if (messageType == "save_plugin_data")
	{
		// Get the save filepath requested
		// Assuming the filepath is the first argument
		juce::String filePath = message[1].getString();

		// filename is the second argument
		juce::String filename = message[2].getString();

		// Get the tags
		std::vector<juce::String> tags = extractTags(message, 3);

		// Get the first tag
		juce::String tag = tags.empty() ? "" : tags[0];

		// Find pluginId that matches the first tag

		for (const auto& instrument : orchestra)
		{
			if (std::find(instrument.tags.begin(), instrument.tags.end(), tag) != instrument.tags.end())
			{
				// Save the plugin data
				DBG("Saving plugin data to file: " + filePath);
				pluginManager.savePluginData(filePath, filename, instrument.pluginInstanceId);

				// Break out of the loop
				break;
			}
		}

		
	}
	else if (messageType == "request_dawServerData")
	{
		// Get the tags
		std::vector<juce::String> tags = extractTags(message, 1);

		// Get the first tag
		juce::String tag = tags.empty() ? "" : tags[0];

		// Find the entry in the orchestra that matches the tag
		for (const auto& instrument : orchestra)
		{
			if (std::find(instrument.tags.begin(), instrument.tags.end(), tag) != instrument.tags.end())
			{
				// Send the MIDI channel back to the sender
				juce::OSCMessage reply("/dawServerData");
				reply.addString(tag);

				// Add the MIDI channel to the reply
				reply.addInt32(instrument.midiChannel);

				// Add the plugin ID to the reply
				reply.addString(instrument.pluginInstanceId);

				// Add the plugin name to the reply
				reply.addString(instrument.pluginName);

				// Add the instrument name to the reply
				reply.addString(instrument.instrumentName);

				// Add the plugin's device id to the reply
				reply.addString(pluginManager.getPluginUniqueId(instrument.pluginInstanceId));

				OSCSender::send(reply);
				DBG("Sent channel data for tag: " + tag);
				break;
			}
		}
	}
	else if (messageType == "sync_request")
	{
		juce::int64 timestamp = getTimestamp(message[1]);
		DBG("Received sync request " << timestamp);

		juce::int64 currentTime = juce::Time::getMillisecondCounter();
		DBG("Current time: " << currentTime);

		timestampOffset = currentTime;
		DBG("Timestamp offset set as current time: " << timestampOffset);

		// Reset the playbackSamplePosition in PluginManager
		pluginManager.resetPlayback();


	}
	else if (messageType == "stop_request")
	{

		DBG("Received stop request ");

		juce::int64 currentTime = juce::Time::getMillisecondCounter();
		DBG("Current time: " << currentTime);

		timestampOffset = currentTime;
		DBG("Timestamp offset set as current time: " << timestampOffset);

		// Reset the playbackSamplePosition in PluginManager
		pluginManager.resetPlayback();
		
	}
	else
	{
		DBG("Error: Unknown OSC message type");
	}
}

juce::int64 Conductor::getTimestamp(const juce::OSCArgument timestampArg)
{
	// Extract string and convert to double
	if (timestampArg.isString())
	{
		juce::String timestampString = timestampArg.getString();
		double timestampInSeconds = timestampString.getDoubleValue(); // We are losing less than a microsecond precision here
		
		return static_cast<juce::int64>(timestampInSeconds * 1000.0); // Convert to milliseconds
	}

	// Handle invalid format
	std::cerr << "Invalid OSC argument for timestamp: expected a string." << std::endl;
	return 0;
}

juce::int64 Conductor::adjustTimestamp(const juce::OSCArgument timestampArg)
{
	auto adjustedStamp = getTimestamp(timestampArg) - timestampOffset; // time elapsed since the sync event in milliseconds

	// Handle negative timestamps
	if (adjustedStamp <= 0)
	{
		std::cerr << "Negative timestamp detected: " << adjustedStamp << std::endl;
		return 0;
	}
	else
	{
		return adjustedStamp;
	}

}



// Helper function to extract tags from the OSC message
std::vector<juce::String> Conductor::extractTags(const juce::OSCMessage& message, int startIndex)
{
	std::vector<juce::String> tags;
    for (int i = startIndex; i < message.size(); ++i)
    {
        if (message[i].isString())
        {
            tags.push_back(message[i].getString());
        }
    }
    return tags;
}

int Conductor::calculateSampleOffsetForMessage(const juce::Time& messageTime, double sampleRate)
{
	juce::Time now = juce::Time::getCurrentTime();  // Get the current time
	auto timeDifferenceMs = now.toMilliseconds() - messageTime.toMilliseconds();

	// Convert the time difference to samples
	int sampleOffset = static_cast<int>((timeDifferenceMs / 1000.0) * sampleRate);

	// Clamp sampleOffset within the block size
	return juce::jlimit(0, 511, sampleOffset);  // Assuming block size is 512 samples, adjust accordingly
}


// Handles incoming OSC messages related to note_on and note_off
void Conductor::handleIncomingNote(juce::String messageType, int channel, int note, int velocity, const juce::String& pluginId, juce::int64& timestamp)
{
	// Create a MIDI message based on the OSC message
	juce::MidiMessage midiMessage;

	if (messageType == "note_on")
	{
		midiMessage = juce::MidiMessage::noteOn(channel + 1, note, (juce::uint8)velocity);  // JUCE channels are 1-based
	}
	else if (messageType == "note_off")
	{
		midiMessage = juce::MidiMessage::noteOff(channel + 1, note);
	}

	// Pass the message and tags to PluginManager
	pluginManager.addMidiMessage(midiMessage, pluginId, timestamp);
}

// Handles incoming OSC program change messages
void Conductor::handleIncomingProgramChange(int channel, int programNumber, const juce::String& pluginId, juce::int64& timestamp)
{
    // Create a MIDI Program Change message
    juce::MidiMessage midiMessage = juce::MidiMessage::programChange(channel + 1, programNumber);

    // Pass the message and tags to PluginManager
    pluginManager.addMidiMessage(midiMessage, pluginId, timestamp);
}

// Handles CC messages
void Conductor::handleIncomingControlChange(int channel, int controllerNumber, int controllerValue, const juce::String& pluginId, juce::int64& timestamp)
{
	// Create a MIDI Control Change message
	juce::MidiMessage midiMessage = juce::MidiMessage::controllerEvent(channel + 1, controllerNumber, controllerValue);

	// Pass the message and tags to PluginManager
	pluginManager.addMidiMessage(midiMessage, pluginId, timestamp);
}

// Add this method to handle pitch bend messages
void Conductor::handleIncomingPitchBend(int channel, int pitchBendValue, const juce::String& pluginId, juce::int64& timestamp)
{
    // Create a MIDI Pitch Bend message
    juce::MidiMessage midiMessage = juce::MidiMessage::pitchWheel(channel + 1, pitchBendValue);
    
    // Pass the message to PluginManager
    pluginManager.addMidiMessage(midiMessage, pluginId, timestamp);
}
// Sync the orchestra list with PluginManager
void Conductor::syncOrchestraWithPluginManager()
{
	DBG("Syncing orchestra with PluginManager");
	if (orchestra.empty())  // Check if orchestra is empty before accessing begin()
	{
		DBG("Orchestra is empty, skipping removal check for plugin");
		
	}
    // Iterate through each instrument in the orchestra and ensure PluginManager matches
    for (auto& instrument : orchestra)
    {
		if (!pluginManager.hasPluginInstance(instrument.pluginInstanceId))
        {
            DBG("Adding plugin to PluginManager: " + instrument.instrumentName);
            pluginManager.instantiatePluginByName(instrument.pluginName, instrument.pluginInstanceId); // Hypothetical function to instantiate plugin by ID
        }
    }

    // Remove plugins from PluginManager that are not in the orchestra
    auto pluginInstances = pluginManager.getPluginInstanceIds();
    for (const auto& pluginId : pluginInstances)
    {
        auto found = std::find_if(orchestra.begin(), orchestra.end(), [&](const InstrumentInfo& instrument) {
            return instrument.pluginInstanceId == pluginId;
            });

        if (found == orchestra.end())
        {
            DBG("Removing plugin from PluginManager: " + pluginId);
            pluginManager.resetPlugin(pluginId);
        }
    }
}

void Conductor::saveOrchestraData(const juce::String& dataFilePath, const std::vector<InstrumentInfo>& selectedInstruments = {})
{
	juce::File dataFile(dataFilePath);

	// Check if the file exists before attempting to clear it
	if (dataFile.existsAsFile())
	{
		dataFile.deleteFile();
	}
	juce::FileOutputStream outputStream(dataFile);

	if (outputStream.openedOk())
	{
		juce::XmlElement rootElement("Orchestra");

		// Iterate through the orchestra vector and add each instrument's data to XML
		for (const auto& instrument : selectedInstruments.empty() ? orchestra : selectedInstruments)
		{
			juce::XmlElement* instrumentElement = rootElement.createNewChildElement("Instrument");
			instrumentElement->setAttribute("instrumentName", instrument.instrumentName);
			instrumentElement->setAttribute("pluginName", instrument.pluginName);
			instrumentElement->setAttribute("pluginInstanceId", instrument.pluginInstanceId);
			instrumentElement->setAttribute("midiChannel", instrument.midiChannel);

			// Save tags as a sub-element
			juce::XmlElement* tagsElement = instrumentElement->createNewChildElement("Tags");
			for (const auto& tag : instrument.tags)
			{
				tagsElement->createNewChildElement("Tag")->setAttribute("value", tag);
			}
		}

		// Write the XML to the output stream
		rootElement.writeToStream(outputStream, "");
		DBG("Orchestra data saved successfully to file: " + dataFilePath);
	}
	else
	{
		DBG("Failed to open file for saving orchestra data: " + dataFilePath);
	}
}

void Conductor::restoreOrchestraData(const juce::String& dataFilePath)
{
	orchestra.clear(); // Clear existing orchestra data
	importOrchestraData(dataFilePath);
}

void Conductor::importOrchestraData(const juce::String& dataFilePath)
{
	juce::File dataFile(dataFilePath);
	juce::XmlDocument xmlDoc(dataFile);
	std::unique_ptr<juce::XmlElement> rootElement(xmlDoc.getDocumentElement());

	if (rootElement != nullptr && rootElement->hasTagName("Orchestra"))
	{
		// Iterate through each "Instrument" element and reconstruct the orchestra vector
		for (auto* instrumentElement : rootElement->getChildIterator())
		{
			if (instrumentElement->hasTagName("Instrument"))
			{
				InstrumentInfo newInstrument;
				newInstrument.instrumentName = instrumentElement->getStringAttribute("instrumentName");
				newInstrument.pluginName = instrumentElement->getStringAttribute("pluginName");
				newInstrument.pluginInstanceId = instrumentElement->getStringAttribute("pluginInstanceId");
				newInstrument.midiChannel = instrumentElement->getIntAttribute("midiChannel");

				// Read tags from the "Tags" sub-element
				if (auto* tagsElement = instrumentElement->getChildByName("Tags"))
				{
					for (auto* tagElement : tagsElement->getChildIterator())
					{
						if (tagElement->hasTagName("Tag"))
						{
							newInstrument.tags.push_back(tagElement->getStringAttribute("value"));
						}
					}
				}

				orchestra.push_back(newInstrument);
			}
		}

		DBG("Orchestra data restored successfully from file: " + dataFilePath);

	}
	else
	{
		DBG("Failed to open or parse XML file for restoring orchestra data: " + dataFilePath);
	}
}

void Conductor::saveAllData(const juce::String& dataFilePath, const juce::String& pluginDescFilePath, const juce::String& orchestraFilePath, const std::vector<InstrumentInfo>& selectedInstruments)
{
	// Create an array of selectedInstances which are the unique pluginInstances in selectedInstruments
	std::vector<juce::String> selectedInstances;
	for (const auto& instrument : selectedInstruments)
	{
		if (std::find(selectedInstances.begin(), selectedInstances.end(), instrument.pluginInstanceId) == selectedInstances.end())
		{
			selectedInstances.push_back(instrument.pluginInstanceId);
			DBG("Selected instance: " + instrument.pluginInstanceId);
		}
	}
	 
	pluginManager.savePluginDescriptionsToFile(pluginDescFilePath, selectedInstances);
	pluginManager.saveAllPluginStates(dataFilePath, selectedInstances);
	saveOrchestraData(orchestraFilePath, selectedInstruments);
}

void Conductor:: upsertAllData(const juce::String& dataFilePath, const juce::String& pluginDescFilePath, const juce::String& orchestraFilePath)
{
	pluginManager.upsertPluginDescriptionsFromFile(pluginDescFilePath);
	pluginManager.restoreAllPluginStates(dataFilePath);
	importOrchestraData(orchestraFilePath);
}

void Conductor::restoreAllData(const juce::String& dataFilePath, const juce::String& pluginDescFilePath, const juce::String& orchestraFilePath)
{
	pluginManager.restorePluginDescriptionsFromFile(pluginDescFilePath);
	pluginManager.restoreAllPluginStates(dataFilePath);
	restoreOrchestraData(orchestraFilePath);
}


// Path: OrchestraTableModel.cpp

OrchestraTableModel::OrchestraTableModel(std::vector<InstrumentInfo>& data, juce::TableListBox& tableRef, MainComponent* mainComponentRef)
	: orchestraData(data), table(tableRef), mainComponent(mainComponentRef) {}

int OrchestraTableModel::getNumRows()
{
	return orchestraData.size();
}

void OrchestraTableModel::paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
	if (rowIsSelected)
		g.fillAll(juce::Colours::lightblue);
	else
		g.fillAll(juce::Colours::lightgrey);
}

void OrchestraTableModel::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
	if (rowNumber < orchestraData.size())
	{
		auto& instrument = orchestraData[rowNumber];
		switch (columnId)
		{
		case 1:
			g.drawText(instrument.instrumentName, 2, 0, width, height, juce::Justification::centredLeft, true);
			break;
		case 2:
			g.drawText(instrument.pluginName, 2, 0, width, height, juce::Justification::centredLeft, true);
			break;
		case 3:
			g.drawText(instrument.pluginInstanceId, 2, 0, width, height, juce::Justification::centredLeft, true);
			break;
		case 4:
			g.drawText(juce::String(instrument.midiChannel), 2, 0, width, height, juce::Justification::centredLeft, true);
			break;
		case 5:
			juce::String tags = convertVectorToString(instrument.tags);
			g.drawText(tags, 2, 0, width, height, juce::Justification::centredLeft, true);

			break;
		}
	}
}

juce::String OrchestraTableModel::convertVectorToString(const std::vector<juce::String>& vector)
{
	juce::String result;
	for (size_t i = 0; i < vector.size(); ++i)
	{
		result += vector[i];
		if (i < vector.size() - 1)
		{
			result += ", ";
		}
	}
	return result;
}

void OrchestraTableModel::selectRow(int row, const juce::ModifierKeys& modifiers)
{
	// Assuming that `table` is a reference or a pointer to the table component.
	// This will allow row selection to be triggered.
	table.selectRowsBasedOnModifierKeys(row, modifiers, true);

	sendTags(row);
	mainComponent->midiManager.startRecording();

	// Copy first tag in Tags to clipboard
	//juce::SystemClipboard::copyTextToClipboard(orchestraData[row].tags.empty() ? "" : orchestraData[row].tags[0]);
}

void OrchestraTableModel::renamePluginInstance(int rowNumber)
{
	// Get the current plugin instance ID for the selected row
	auto& instrument = mainComponent->getConductor().orchestra[rowNumber];
	juce::String currentPluginInstanceId = instrument.pluginInstanceId;

	// Create the RenamePluginDialog
	auto* dialog = new RenamePluginDialog(currentPluginInstanceId);
	// Display the dialog using DialogWindow::LaunchOptions
	juce::DialogWindow::LaunchOptions options;
	options.content.setOwned(dialog);
	options.dialogTitle = "Rename Plugin Instance";
	options.dialogBackgroundColour = juce::Colours::lightgrey;
	options.escapeKeyTriggersCloseButton = true;
	options.useNativeTitleBar = true;
	options.resizable = false;
	auto* dw = options.launchAsync();  // returns the DialogWindow*

	// Set up the callback for when the dialog is closed
	dialog->onDialogResult = [this, &instrument, currentPluginInstanceId, dialog, dw](bool accepted)
		{
			if (accepted) // User clicked "OK"
			{
				// Get the new plugin instance ID from the dialog
				juce::String newPluginInstanceId = dialog->getSelectedPluginInstanceId();

				// Validate the new ID
				if (newPluginInstanceId.isEmpty())
				{
					juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
						"Rename Plugin Instance",
						"Plugin Instance ID cannot be empty.");
					return;
				}

				// Update all rows with the same pluginInstanceId
				for (auto& row : mainComponent->getConductor().orchestra)
				{
					if (row.pluginInstanceId == currentPluginInstanceId)
					{
						row.pluginInstanceId = newPluginInstanceId;
					}
				}

				// Update the PluginManager
				if (mainComponent->getPluginManager().hasPluginInstance(currentPluginInstanceId))
				{
					mainComponent->getPluginManager().renamePluginInstance(currentPluginInstanceId, newPluginInstanceId);
				}

				// Refresh the table to reflect the changes
				mainComponent->orchestraTable.updateContent();
			}

			// Close the dialog
			dw->exitModalState(0); // Close the dialog
			
		};


}


void OrchestraTableModel::sendTags(int row)
{
	// Send the selected tags to the OSC sender
	if (row >= 0 && row < orchestraData.size())
	{
		const InstrumentInfo& instrument = orchestraData[row];
		mainComponent->getConductor().sendOSCMessage(instrument.tags);
	}
}

int OrchestraTableModel::getSelectedMidiChannel()
{
	// Get the set of selected rows
	juce::SparseSet<int> selectedRows = table.getSelectedRows();

	// Get MIDI channel from the first selected row
	if (selectedRows.size() > 0)
	{
		int midiChannel = orchestraData[selectedRows[0]].midiChannel;
		return midiChannel;
	}
	else
	{
		// return Channel 1
		return 1;
	}

}

juce::String OrchestraTableModel::getSelectedPluginId()
{
	// Get the pluginId of the first selected rows
	juce::SparseSet<int> selectedRows = table.getSelectedRows();

	if (selectedRows.size() > 0)
	{
		juce::String pluginId = orchestraData[selectedRows[0]].pluginInstanceId;
		return pluginId;
	}
	else
	{
		// return an empty string
		return juce::String();
	}
}

juce::String OrchestraTableModel::getText(int columnNumber, int rowNumber) const
{
	if (rowNumber < 0 || rowNumber >= orchestraData.size())
	{
		// Returning empty string or consider logging an error
		return "Invalid row number";
	}

	const InstrumentInfo& info = orchestraData[rowNumber];
	switch (columnNumber)
	{
	case 1: return info.instrumentName;
	case 2: return info.pluginName;
	case 3: return info.pluginInstanceId;
	case 4: return juce::String(info.midiChannel);
	case 5: return convertVectorToString(info.tags);
	default: return "Invalid column number";
	}
}

void OrchestraTableModel::setText(int columnNumber, int rowNumber, const juce::String& newText)
{
	InstrumentInfo& info = orchestraData[rowNumber];
	switch (columnNumber)
	{
	case 1: info.instrumentName = newText; break;
	case 2: info.pluginName = newText; break;
	case 3: info.pluginInstanceId = newText; break;
	case 4: info.midiChannel = newText.getIntValue(); break;
	case 5:
	{
		juce::StringArray tagsArray;
		tagsArray.addTokens(newText, ",", "");
		info.tags.clear();
		for (int i = 0; i < tagsArray.size(); i++)
		{
			// strip leading and trailing spaces
			juce::String stripped = tagsArray[i].trim();
			info.tags.push_back(stripped);
		}
		break;

	}
	default: break;
	}
}

// Refresh component for editable cells

juce::Component* OrchestraTableModel::refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate)
{
	auto* textLabel = dynamic_cast<EditableTextCustomComponent*>(existingComponentToUpdate);

	if (textLabel == nullptr)
		textLabel = new EditableTextCustomComponent(*this);

	textLabel->setRowAndColumn(rowNumber, columnId);
	return textLabel;
}



// Path: EditableTextCustomComponent.cpp

EditableTextCustomComponent::EditableTextCustomComponent(OrchestraTableModel& ownerRef)
	: owner(ownerRef), row(-1), columnId(-1) // Initialize row and columnId to -1
{
	setEditable(false, true, false);  // Allow double-click to edit
	setColour(juce::Label::textColourId, juce::Colours::black); // Set text colour to black
}

void EditableTextCustomComponent::mouseDown(const juce::MouseEvent& event)
{
	if (event.mods.isRightButtonDown())
	{
		switch (columnId)
		{
		case 1:
			showContextMenu_name();
			break;
		case 3:
			showContextMenu_pluginInstances();
			break;
		case 4:
			showContextMenu_midiChannels();
			break;
		case 5:
			showContextMenu_tags();
			break;
		}

	}
	else
	{
		// Existing behavior for left-click (row selection)
		if (row != -1)
		{
			owner.selectRow(row, event.mods);
		}
		juce::Label::mouseDown(event);  // Call the base class method to retain default behavior
	}
}

void EditableTextCustomComponent::showContextMenu_name()
{
	juce::PopupMenu contextMenu;

	// Add menu options - you can customize these as per your requirements
	contextMenu.addItem("Save Selected", [this] {save_selection(); });
	contextMenu.addItem("Insert from File", [this] {owner.mainComponent->restoreProject(true); });

	// Show the menu at the current mouse position
	contextMenu.showAt(this);
}

void EditableTextCustomComponent::save_selection()
{
	// Get the set of selected rows
	juce::SparseSet<int> selectedRows = owner.table.getSelectedRows();

	// Initialise vector
	std::vector<InstrumentInfo> selectedInstruments;

	// Add selected instruments to the vector
	for (int i = 0; i < selectedRows.size(); ++i)
	{
		int selectedRow = selectedRows[i];
		if (selectedRow >= 0 && selectedRow < owner.orchestraData.size())
		{
			InstrumentInfo& instrument = owner.orchestraData[selectedRow];
			selectedInstruments.push_back(instrument);
		}
	}

	// Save the selected instruments
	owner.mainComponent->saveProject(selectedInstruments);

}

std::vector<juce::String> presetTags = {
	// General tags
	"Drums",

	// Orchestra sections and solos
	"Scoring Piano",
	"String Ensemble",
	"Woodwind Quartet",
	"Brass Section",
	"Percussion",
	"Solo Violin",
	"Solo Cello",
	"Solo Flute",
	"Solo Trumpet",
	"String Quartet",
	"Brass Quartet",
	"Woodwind Ensemble",
	"Symphonic Orchestra",

	// Vocals
	"Soprano",
	"Alto",
	"Tenor",
	"Bass",
	"Choir",
	"Male Choir",
	"Female Choir",
	"Vocal Solo",
	"Opera",

	// Synth types
	"Ambient Synth",
	"Arpeggiated Synth",
	"FM Synth",
	"Wavetable Synth",
	"Polyphonic Synth",
	"Electronic Synth",
	"Synth Bass",
	"Pad Synth",
	"Mono Synth",
	"Lead Synth",
	"Lofi Synth",
	"Chiptune Synth"
};


void EditableTextCustomComponent::showContextMenu_pluginInstances()
{
	juce::PopupMenu contextMenu;

	// Add menu options - you can customize these as per your requirements
	contextMenu.addItem("Iterate and renumber first text", [this] {iterate_pluginInstances(); });
	contextMenu.addItem("Rename Plugin Instance", [this]()
		{
			if (row != -1) // Ensure the row is valid
			{
				owner.renamePluginInstance(row);
			}
		});
	contextMenu.addItem("Rename References for Selected Rows", [this]()
		{
			renameReferencesForSelectedRows();
		});
	contextMenu.addItem("Purge Plugin instance", [this]()
		{
			if (row != -1) // Ensure the row is valid
			{
				owner.mainComponent->getPluginManager().resetPlugin(owner.getSelectedPluginId());
				owner.table.updateContent();
			}
		});
	// Show the menu at the current mouse position
	contextMenu.showAt(this);
}

void EditableTextCustomComponent::renameReferencesForSelectedRows()
{
	// Get the set of selected rows
	juce::SparseSet<int> selectedRows = owner.table.getSelectedRows();

	if (selectedRows.size() == 0)
	{
		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
			"Rename References",
			"No rows are selected.");
		return;
	}

	// Prompt the user for a new name
	juce::AlertWindow renameWindow("Rename References",
		"Enter a new name for all selected rows:",
		juce::AlertWindow::NoIcon);
	renameWindow.addTextEditor("newReferenceName", "", "New Name:");
	renameWindow.addButton("OK", 1);
	renameWindow.addButton("Cancel", 0);

	if (renameWindow.runModalLoop() == 1) // If "OK" is pressed
	{
		juce::String newReferenceName = renameWindow.getTextEditor("newReferenceName")->getText();

		if (newReferenceName.isEmpty())
		{
			juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
				"Rename References",
				"Name cannot be empty.");
			return;
		}

		// Apply the new name to all selected rows
		for (int i = 0; i < selectedRows.size(); ++i)
		{
			int selectedRow = selectedRows[i];
			if (selectedRow >= 0 && selectedRow < owner.orchestraData.size())
			{
				auto& instrument = owner.orchestraData[selectedRow];
				instrument.pluginInstanceId = newReferenceName;
			}
		}

		// Refresh the table to reflect the changes
		owner.table.updateContent();
	}
}


void EditableTextCustomComponent::iterate_pluginInstances()
{
	// Get the set of selected rows
	juce::SparseSet<int> selectedRows = owner.table.getSelectedRows();

	// get the text of the first selected row
	juce::String firstSelectedRowText = owner.getText(columnId, selectedRows[0]);

	// Iterate through selected rows and update the specified column
	for (int i = 0; i < selectedRows.size(); ++i)
	{
		int selectedRow = selectedRows[i];
		if (selectedRow >= 0 && selectedRow < owner.orchestraData.size())
		{
			InstrumentInfo& instrument = owner.orchestraData[selectedRow];

			// new field name will be the first selected row text with the row number appended
			instrument.pluginInstanceId = firstSelectedRowText + juce::String(i);
		}
	}

	// Update table content to reflect the changes in the UI
	owner.table.updateContent();
}

void EditableTextCustomComponent::showContextMenu_midiChannels()
{
	juce::PopupMenu contextMenu;
	juce::PopupMenu midiChannelsMenu;
	// Add menu options - you can customize these as per your requirements
	for (int i = 1; i <= 16; ++i)
	{
		midiChannelsMenu.addItem(juce::String(i), [this, i] {actionContextSelection(juce::String(i), 4); });
	}

	// Add menu options - you can customize these as per your requirements
	
	contextMenu.addItem("Sequence MIDI Channels", [this] {
		// Get the set of selected rows
		juce::SparseSet<int> selectedRows = owner.table.getSelectedRows();

		// Iterate through the selected rows and update the specified column
		for (int i = 0; i < selectedRows.size(); ++i)
		{
			int selectedRow = selectedRows[i];
			if (selectedRow >= 0 && selectedRow < owner.orchestraData.size())
			{
				InstrumentInfo& instrument = owner.orchestraData[selectedRow];
				instrument.midiChannel = i + 1;  // Sequential MIDI channels starting from 1
			}
		}
		
		// Update table content to reflect the changes in the UI
		owner.table.updateContent();
		});
	contextMenu.addSubMenu("Replace MIDI Channel", midiChannelsMenu);

	// Show the menu at the current mouse position
	contextMenu.showAt(this);
}


void EditableTextCustomComponent::showContextMenu_tags()
{
	juce::PopupMenu contextMenu;

	// Add menu option for "Replace Tags" with a lambda to call getTagsPresetList
	contextMenu.addItem("Add to Tags", [this, &contextMenu] {
		getTagsPresetList([this](const juce::String& tag, int columnId) { 
			// Add to existing tags in cell
			juce::String existingTags = getText();
			juce::String newTags = existingTags + ", " + tag;
			actionContextSelection(newTags, columnId); });
		});

	// Add menu option for "Add to Tags" with a lambda to call getTagsPresetList
	contextMenu.addItem("Replace Tags", [this, &contextMenu] {
		getTagsPresetList([this](const juce::String& tag, int columnId) { actionContextSelection(tag, columnId); });
		});

	// Show the menu at the current mouse position
	contextMenu.showAt(this);
}


void EditableTextCustomComponent::getTagsPresetList(std::function<void(const juce::String&, int)> callback)
{
	juce::PopupMenu presetTagsMenu;

	// Create presetTags from SQLlite database

	// Add menu options - you can customize these as per your requirements
	for (const auto& tag : presetTags)
	{
		// Capture callback to use it within the lambda
		presetTagsMenu.addItem(tag, [callback, tag] { callback(tag, 5); });
	}
	presetTagsMenu.showAt(this);
}


void EditableTextCustomComponent::actionContextSelection(const juce::String& text, int columnId)
{
	// Get the set of selected rows
	juce::SparseSet<int> selectedRows = owner.table.getSelectedRows();

	// Iterate through selected rows and update the specified column
	for (int i = 0; i < selectedRows.size(); ++i)
	{
		int selectedRow = selectedRows[i];
		if (selectedRow >= 0 && selectedRow < owner.orchestraData.size())
		{
			InstrumentInfo& instrument = owner.orchestraData[selectedRow];

			// Update the field based on the columnId
			switch (columnId)
			{
			case 1:  // Assuming column 1 is for Instrument Name
				instrument.instrumentName = text;
				break;
			case 2:  // Assuming column 2 is for Plugin Name
				instrument.pluginName = text;
				break;
			case 3:  // Assuming column 3 is for Plugin Instance ID
				instrument.pluginInstanceId = text;
				break;
			case 4:  // Assuming column 4 is for MIDI Channel
				instrument.midiChannel = text.getIntValue();
				break;
			case 5:  // Assuming column 5 is for Tags
			{
				juce::StringArray tagsArray;
				tagsArray.addTokens(text, ",", "");
				instrument.tags.clear();
				for (int i = 0; i < tagsArray.size(); i++)
				{
					juce::String stripped = tagsArray[i].trim();
					instrument.tags.push_back(stripped);
				}
				break;
			}
			default:
				DBG("Unknown column ID: " + juce::String(columnId));
				break;
			}
		}
	}

	// Update table content to reflect the changes in the UI
	owner.table.updateContent();
}


void EditableTextCustomComponent::textWasEdited()
{
	// Update the underlying data when the text is edited
	if (columnId != -1 && row != -1)
	{
		owner.setText(columnId, row, getText());
	}
}

void EditableTextCustomComponent::setRowAndColumn(int newRow, int newColumn)
{
	row = newRow;
	columnId = newColumn;
	setText(owner.getText(columnId, row), juce::dontSendNotification);
}
