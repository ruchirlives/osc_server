/*
  ==============================================================================

    PluginManager.cpp
    Created: 16 Sep 2024 4:03:37pm
    Author:  Desktop

  ==============================================================================
*/

#include "PluginManager.h"
#include "MainComponent.h"
#include <unordered_map>
#include "VST3Visitor.h"
#include <juce_audio_processors/juce_audio_processors.h>

HostPlayHead hostPlayHead;
bool PluginManager::playStartIssued = false;
bool PluginManager::midiStartSent = false;

std::unordered_map<juce::String, juce::AudioPluginInstance::HostedParameter*> buildParameterMap(const juce::AudioPluginInstance* pluginInstance)
{
    std::unordered_map<juce::String, juce::AudioPluginInstance::HostedParameter*> parameterMap;
    int numParameters = pluginInstance->getParameters().size();
    for (int i = 0; i < numParameters; ++i)
    {
        juce::AudioPluginInstance::HostedParameter* parameter = pluginInstance->getHostedParameter(i);
        if (parameter != nullptr)
        {
            juce::String parameterID = parameter->getParameterID();
            parameterMap[parameterID] = parameter;
        }
    }

    return parameterMap;
}

void printParameterNames(const juce::AudioPluginInstance* pluginInstance)
{
    auto parameters = pluginInstance->getParameters();
    for (int i = 0; i < parameters.size(); ++i)
    {
        if (auto* parameter = parameters[i])
        {
            juce::String parameterName = parameter->getName(128);
            float parameterValue = parameter->getValue(); // Normalized value (0.0 to 1.0)
            DBG("Parameter " + juce::String(i) + ": " + parameterName + " = " + juce::String(parameterValue));
        }
    }
}



PluginManager::PluginManager(MainComponent* mainComponent, juce::CriticalSection& criticalSection, juce::MidiBuffer& midiBuffer)
	: mainComponent(mainComponent), midiCriticalSection(criticalSection), incomingMidi(midiBuffer)
{
    formatManager.addFormat(new juce::VST3PluginFormat());  // Adds only VST3 format to the format manager
    deviceManager.initialise(4, 32, nullptr, true); // Initialize with 4 input and 8 output channels.
    setAudioChannels(4, 32); // Set input and output channels to 4 and 8, respectively.

}

PluginManager::~PluginManager()
{
    shutdownAudio();
}

void PluginManager::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    for (auto& [pluginId, pluginInstance] : pluginInstances)
    {
        if (pluginInstance != nullptr)
        {
			pluginInstance->prepareToPlay(sampleRate, samplesPerBlockExpected);

        }
    }
}

void PluginManager::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // 1) Update the shared play-head before any plugin processes

	//DBG currentBPM and myBPM
	//DBG("Current BPM: " << currentBpm);
    
    auto& pos = hostPlayHead.positionInfo;

    pos = {};  // clear all flags
    pos.setBpm(currentBpm);
    pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{ 4, 4 });
    pos.setTimeInSamples(playbackSamplePosition);
    pos.setTimeInSeconds(playbackSamplePosition / currentSampleRate);
    pos.setPpqPosition(playbackSamplePosition
        * (currentBpm / 60.0)
        / currentSampleRate);
    
	//// client side pointer check of hostPlayHead.positionInfo
	//double bpm = hostPlayHead.positionInfo.getBpm() ? *hostPlayHead.positionInfo.getBpm() : 0.0;

    // clear the output buffer
    bufferToFill.clearActiveBufferRegion();

    const juce::ScopedLock sl(midiCriticalSection);
    double sampleRate = deviceManager.getCurrentAudioDevice()->getCurrentSampleRate();

    // 2) Process each plugin once, in a single loop
    for (auto& [pluginId, pluginInstance] : pluginInstances)
    {
        if (!pluginInstance)
            continue;

        // d) prepare a per-plugin tempBuffer with correct channel count
        int numOut = pluginInstance->getTotalNumOutputChannels();
        juce::AudioBuffer<float> tempBuffer(numOut, bufferToFill.numSamples);
        tempBuffer.clear();

        // e) gather tagged MIDI for this plugin
        juce::MidiBuffer matchingMessages;
        bool isStartingPlayback = (playbackSamplePosition == 0);
        const int graceWindow = bufferToFill.numSamples; // One buffer's worth

        for (auto it = taggedMidiBuffer.begin(); it != taggedMidiBuffer.end();)
        {
            const auto& tm = *it;
            if (tm.pluginId == pluginId)
            {
                int offset = 0;

                // Immediate message (no timestamp)
                if (tm.timestamp == 0)
                {
                    matchingMessages.addEvent(tm.message, 0);
                    it = taggedMidiBuffer.erase(it);
                    continue;
                }

                // Convert ms to absolute sample position
                auto absPos = static_cast<juce::int64>((tm.timestamp / 1000.0) * sampleRate);
                offset = static_cast<int>(absPos - playbackSamplePosition);

                // Handle early notes gracefully if at playback start
                if ((isStartingPlayback && offset >= -graceWindow && offset < bufferToFill.numSamples)
                    || (offset >= 0 && offset < bufferToFill.numSamples))
                {
                    matchingMessages.addEvent(tm.message, juce::jmax(0, offset)); // Clamp offset to 0 if early
                    it = taggedMidiBuffer.erase(it);
                    continue;
                }
            }

            ++it; // Keep unscheduled messages
        }

        // merge in live incoming MIDI if this is the selected plugin
        if (pluginId == mainComponent->getOrchestraTableModel().getSelectedPluginId())
            matchingMessages.addEvents(incomingMidi,
                0,
                bufferToFill.numSamples,
                bufferToFill.startSample);


        // f) run the plugin
        pluginInstance->processBlock(tempBuffer, matchingMessages);

        // g) mix plugin output back into the host buffer
        for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
        {
            int outCh = ch < tempBuffer.getNumChannels() ? ch : tempBuffer.getNumChannels() - 1;
            bufferToFill.buffer->addFrom(ch,
                bufferToFill.startSample,
                tempBuffer,
                outCh,
                0,
                bufferToFill.numSamples);
        }
    }

    // clear incoming MIDI and advance the host clock
    incomingMidi.clear();
    playbackSamplePosition += bufferToFill.numSamples;
}



void PluginManager::releaseResources()
{
    for (auto& [pluginId, pluginInstance] : pluginInstances)
    {
        if (pluginInstance != nullptr)
        {
            pluginInstance->releaseResources();
        }
    }
}

void PluginManager::setBpm(double bpm)
{
    currentBpm = bpm;
}

juce::PluginDescription PluginManager::getDescFromName(const juce::String& name)
{
    for (int i = 0; i < knownPluginList.getNumTypes(); ++i)
    {
		juce::PluginDescription* desc = knownPluginList.getType(i);
        if (desc->name == name)
        {
			return *desc;
		}
	}
	return juce::PluginDescription();
}

void PluginManager::instantiatePluginByName(const juce::String& name, const juce::String& pluginId)
{
	juce::PluginDescription desc = getDescFromName(name);
    if (desc.name.isNotEmpty())
    {
		instantiatePlugin(&desc, pluginId);
	}
    else
    {
		DBG("Plugin not found: " << name);
	}
}

// Method to get plugin instance IDs as a StringArray
juce::StringArray PluginManager::getPluginInstanceIds() const
{
    juce::StringArray instanceIds;
    for (const auto& pluginPair : pluginInstances)
    {
        instanceIds.add(pluginPair.first);
    }
    return instanceIds;
}

void PluginManager::instantiatePlugin(juce::PluginDescription* desc, const juce::String& pluginId)
{
    juce::String errorMessage;

    auto sampleRate = deviceManager.getAudioDeviceSetup().sampleRate;
    auto blockSize = deviceManager.getAudioDeviceSetup().bufferSize;

    std::unique_ptr<juce::AudioPluginInstance> instance = formatManager.createPluginInstance(
        *desc, sampleRate, blockSize, errorMessage);

    if (instance != nullptr)
    {
        pluginInstances[pluginId] = std::move(instance);
        pluginInstances[pluginId]->setPlayHead(&hostPlayHead);
        pluginInstances[pluginId]->prepareToPlay(sampleRate, blockSize);
        DBG("Plugin instantiated successfully: " << pluginId);
    }
    else
    {
        DBG("Error instantiating plugin: " << errorMessage);
    }
}

void PluginManager::openPluginWindow(juce::String pluginId)
{
	if (pluginWindows.find(pluginId) == pluginWindows.end() && pluginInstances.find(pluginId) != pluginInstances.end())
	{
        pluginWindows[pluginId] = std::make_unique<PluginWindow>(pluginInstances[pluginId].get());
	}
	else if (pluginWindows.find(pluginId) != pluginWindows.end())
	{
		pluginWindows[pluginId]->setVisible(true);
		getPluginData(pluginId);
	}
	else
	{
		DBG("Plugin window not found: " << pluginId);
		listPluginInstances();
	}
}

void PluginManager::instantiateSelectedPlugin(juce::PluginDescription* desc)
{
    juce::Uuid uuid;
    juce::String pluginId = "Selection 1";

	if (pluginInstances.find(pluginId) == pluginInstances.end())
	{
		instantiatePlugin(desc, pluginId);
	}
	else
	{
		DBG("Plugin already instantiated: " << pluginId);
	}
}

juce::String PluginManager::getPluginData(juce::String pluginId)
{
	juce::String response = "Plugin not found.";
	if (pluginInstances.find(pluginId) != pluginInstances.end())
	{
		juce::PluginDescription desc = pluginInstances[pluginId]->getPluginDescription();
		response = desc.name;
		DBG("Plugin data found: " << response);
		
	}
    return response;
}

void PluginManager::resetPlugin(const juce::String& pluginId)
{
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        // Check if there is an associated editor and safely delete it
        if (auto* editor = pluginInstances[pluginId]->getActiveEditor())
        {
            editor->setVisible(false); // Hide the editor before deletion to avoid UI-related issues
            delete editor;
            editor = nullptr; // Ensure dangling pointers are set to nullptr
        }

        // delete plugin instance
        pluginInstances[pluginId].reset();

        // Erase entries from maps after safely resetting the plugin
        pluginInstances.erase(pluginId);
        pluginWindows.erase(pluginId);
        DBG("Plugin reset: " << pluginId);
    }
}

void PluginManager::resetAllPlugins()
{
    // Iterate over a copy of the keys, as we modify the original map
    std::vector<juce::String> pluginIds;
    for (const auto& [pluginId, _] : pluginInstances)
    {
        pluginIds.push_back(pluginId);
    }

    for (const auto& pluginId : pluginIds)
    {
        resetPlugin(pluginId);
    }

    pluginInstances.clear();
    pluginWindows.clear();
    DBG("All plugins have been reset.");
}



bool PluginManager::hasPluginInstance(const juce::String& pluginId)
{
	if (pluginInstances.find(pluginId) != pluginInstances.end())
	{
		return true;
	}
    return false;
}

void PluginManager::listPluginInstances()
{
	for (const auto& [pluginId, pluginInstance] : pluginInstances)
	{
		DBG("Plugin ID: " << pluginId);
		if (pluginInstance != nullptr)
		{
			DBG("Plugin Name: " << pluginInstance->getName());
		}
	}
}

void PluginManager::savePluginData(const juce::String& dataFilePath, const juce::String& filename, const juce::String& pluginId)
{
	// Get plugin unique ID
	juce::String uniqueId = getPluginUniqueId(pluginId);

    // Construct full file path
	juce::String fullFilePath = dataFilePath + "/" + filename + ".vstpreset";
    
    // Save plugin data to binary file as a .vstpreset
	juce::File dataFile(fullFilePath);

	// Check if the file exists before attempting to clear it
	if (dataFile.existsAsFile())
	{
		dataFile.deleteFile();
	}

	juce::FileOutputStream dataOutputStream(dataFile);

	// Check if the file opened successfully
	if (dataOutputStream.openedOk())
	{
		// Create VST3Visitor instance
		CustomVST3Visitor visitor;

		// Get the plugin instance
		juce::AudioPluginInstance* plugin = pluginInstances[pluginId].get();

		// Visit the plugin instance
		plugin->getExtensions(visitor);

		// Get and write the plugin state
		juce::MemoryBlock state = visitor.presetData;

		// Check if the plugin state is empty
		if (state.getSize() == 0)
		{
			DBG("Plugin state is empty.");
			return;
		}

		dataOutputStream.write(state.getData(), state.getSize());
		DBG("Plugin data saved successfully to vstpreset file.");
	}
	else
	{
		DBG("Failed to open file for saving plugin data.");
	}
}

juce::String PluginManager::getPluginUniqueId(const juce::String& pluginId)
{
    auto it = pluginInstances.find(pluginId);
    if (it == pluginInstances.end() || it->second == nullptr)
    {
        DBG("Error: Plugin ID not found or plugin instance is null.");
        return "Invalid Plugin ID";
    }

    // Get the plugin instance
    juce::AudioPluginInstance* plugin = it->second.get();

    // Get the plugin description
    juce::PluginDescription description = plugin->getPluginDescription();

	// Create Unique ID
	juce::String uniqueId = description.createIdentifierString();

	return uniqueId;
	
}

void PluginManager::scanPlugins(juce::FileSearchPath searchPaths)
{
    knownPluginList.clear();
    DBG("Scanning for VST3 plugins in " << searchPaths.toString());

    // Create a persistent VST3PluginFormat instance
    juce::VST3PluginFormat vst3Format;

    juce::PluginDirectoryScanner scanner(knownPluginList, vst3Format, searchPaths, true, juce::File(), false);

    juce::String nameOfPluginBeingScanned;
    while (scanner.scanNextFile(true, nameOfPluginBeingScanned))
    {
        juce::Thread::sleep(100);
        if (knownPluginList.getNumTypes() > 20)
        {
            break;
        }
    }

    DBG("Scanning completed. " << knownPluginList.getNumTypes() << " VST3 Plugins Found");
	savePluginListToFile();
	DBG("Plugin list saved to file.");

}

void PluginManager::savePluginListToFile()
{
    // Use JUCE to save the plugin list to a file using createXml
    std::unique_ptr<juce::XmlElement> pluginListXml;
    pluginListXml = knownPluginList.createXml();

    // Ensure the XmlElement was created successfully
    if (pluginListXml != nullptr)
    {
        // Write the XML to a file
        juce::File pluginListFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("PluginList.xml");

        // Check if the file exists before attempting to clear it
		if (pluginListFile.existsAsFile())
		{
			pluginListFile.deleteFile();
		}

        // Write the XML to the file directly
        bool existed = pluginListFile.existsAsFile();
        pluginListXml->writeTo(pluginListFile);

        // Set file times
        if (!existed)
            pluginListFile.setCreationTime(juce::Time::getCurrentTime());

        pluginListFile.setLastModificationTime(juce::Time::getCurrentTime());
        pluginListFile.setLastAccessTime(juce::Time::getCurrentTime());
    }
    else
    {
        // Handle error: failed to create XML element
        DBG("Failed to create XML from plugin list");
    }
}

bool PluginManager::loadPluginListFromFile()
{
    // Load the plugin list from an XML file
    juce::File pluginListFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("PluginList.xml");

    if (pluginListFile.existsAsFile())
    {
        std::unique_ptr<juce::XmlElement> pluginListXml = juce::XmlDocument::parse(pluginListFile);

        if (pluginListXml != nullptr)
        {
            knownPluginList.recreateFromXml(*pluginListXml);
            pluginListFile.setLastAccessTime(juce::Time::getCurrentTime());
            return true;
        }
        else
        {
            // Handle error: failed to parse XML
            DBG("Failed to parse XML from PluginList.xml");
            return false;
        }
    }
    else
    {
        // Handle error: file does not exist
        DBG("PluginList.xml does not exist");
        return false;
    }
}


void PluginManager::addMidiMessage(const juce::MidiMessage& message, const juce::String& pluginId, juce::int64& adjustedTimestamp)
{
    const juce::ScopedLock sl(midiCriticalSection); // Lock the critical section to ensure thread safety
    // Add the tagged MIDI message to the buffer with a timestamp
    
    taggedMidiBuffer.emplace_back(message, pluginId, adjustedTimestamp);
    // DBG("Added tagged MIDI message with " << pluginIds.size() << " pluginIds.");
	// DBG("Added timestamp: " << absoluteTimeMs << "from " << timestamp);
}

void PluginManager::resetPlayback()
{
	playbackSamplePosition = 0;
	// Also clear the taggedMidiBuffer
	taggedMidiBuffer.clear();
	// And stop any currently playing notes
	//for (const auto& [pluginId, pluginInstance] : pluginInstances)
	//{
	//	if (pluginInstance != nullptr)
	//	{
	//		pluginInstance->
	//	}
	//}
}

juce::int8 PluginManager::getNumInstances(std::vector<juce::String>& instances)
{
    juce::int8 numInstances = 0;

    if (instances.empty())
    {
        numInstances = pluginInstances.size();
        DBG("Number of total plugins if selection not used: " << juce::String(numInstances));
    }
    else
    {
        // Find the number of instances that are also in the pluginInstances map

        for (const auto& instance : instances)
        {
            if (pluginInstances.find(instance) != pluginInstances.end())
            {
                numInstances++;
				DBG("Instance found: " << instance);
            }
        }
    }
	DBG("Count of instances to save: " << juce::String(numInstances));
	return numInstances;
}

void PluginManager::savePluginDescriptionsToFile(const juce::String& dataFilePath, std::vector<juce::String> instances)
{
	// Save plugin descriptions to binary file paired with the plugin ID
    juce::File dataFile(dataFilePath);

    // Check if the file exists before attempting to clear it
	if (dataFile.existsAsFile())
	{
		dataFile.deleteFile();
	}

	juce::FileOutputStream dataOutputStream(dataFile);

	if (dataOutputStream.openedOk())
	{
		// Save the number of pluginsInstances
        juce::int8 numInstances = getNumInstances(instances);
		dataOutputStream.writeInt(numInstances);

		// Iterate over each plugin instance and save its id and description pair
		for (const auto& pluginPair : pluginInstances)
		{
			// Check if instances is empty or if the pluginId is in the instances vector
			if (instances.empty() || std::find(instances.begin(), instances.end(), pluginPair.first) != instances.end())
			{
				// Write plugin ID
				dataOutputStream.writeString(pluginPair.first);

				// Get and write plugin description
				juce::PluginDescription desc = pluginPair.second->getPluginDescription();
				dataOutputStream.writeString(desc.name);
			}
		}
		DBG("All plugin descriptions saved successfully to binary file.");
	}
	else
	{
		DBG("Failed to open file for saving plugin descriptions.");
	}

}

void PluginManager::restorePluginDescriptionsFromFile(const juce::String& dataFilePath)
{
	// Restore plugin descriptions from binary file dataFilePath
    juce::File dataFile(dataFilePath);
	juce::FileInputStream dataInputStream(dataFile);
	if (dataInputStream.openedOk())
	{
		// Read the number of plugins
		int numPluginInstances = dataInputStream.readInt();

		// iterate plugin instances and remove them
		resetAllPlugins();

		// Iterate over each plugin instance and restore its description
		for (int i = 0; i < numPluginInstances; ++i)
		{
			juce::String pluginId = dataInputStream.readString();
			juce::String name = dataInputStream.readString();
			juce::PluginDescription desc;
			desc.name = name;

			instantiatePluginByName(name, pluginId);
		}


		DBG("All plugin descriptions restored successfully from binary file.");
	}
	else
	{
		DBG("Failed to open file for restoring plugin descriptions.");
	}
}

void PluginManager::upsertPluginDescriptionsFromFile(const juce::String& dataFilePath)
{
    // Restore plugin descriptions from binary file dataFilePath
    juce::File dataFile(dataFilePath);
    juce::FileInputStream dataInputStream(dataFile);
    if (dataInputStream.openedOk())
    {
        // Read the number of plugins
        int numPluginInstances = dataInputStream.readInt();

        // Iterate over each plugin instance and restore its description
        for (int i = 0; i < numPluginInstances; ++i)
        {
            juce::String pluginId = dataInputStream.readString();
            juce::String name = dataInputStream.readString();

            // first check if the pluginId is not already in the pluginInstances map
			if (pluginInstances.find(pluginId) == pluginInstances.end())
			{
				instantiatePluginByName(name, pluginId);
			}


            DBG("All plugin descriptions upserted successfully from binary file.");
        }
	}
    else
    {
        DBG("Failed to open file for restoring plugin descriptions.");
    }
}


juce::MemoryBlock PluginManager::getPluginState(const juce::String& pluginId)
{
    juce::MemoryBlock state;
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        pluginInstances[pluginId]->getStateInformation(state);
        juce::String stateString = state.toString(); // Attempt to convert to a readable string
    }
    else
	{
		DBG("Plugin not found: " << pluginId);
	}
    return state;
}


void PluginManager::restorePluginState(const juce::String& pluginId, const juce::MemoryBlock& state)
{
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        pluginInstances[pluginId]->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        DBG("Plugin state restored for: " << pluginId);
    }
}

void PluginManager::saveAllPluginStates(const juce::String& dataFilePath, std::vector<juce::String> instances)
{
    // Save plugin states to binary file
    juce::File dataFile(dataFilePath);

    // Check if the file exists before attempting to clear it
    if (dataFile.existsAsFile())
    {
        dataFile.deleteFile();
    }

    juce::FileOutputStream dataOutputStream(dataFile);

    if (dataOutputStream.openedOk())
    {
        // Save the number of plugins
		juce::int8 numInstances = getNumInstances(instances);
        DBG("We are sending instances numbering: " << juce::String(numInstances));
        dataOutputStream.writeInt(numInstances);
        

        // Iterate over each plugin instance and save its state
        for (const auto& [pluginId, pluginInstance] : pluginInstances)
        {
			// Check if instances is empty or if the pluginId is in the instances vector
			if (instances.empty() || std::find(instances.begin(), instances.end(), pluginId) != instances.end())
			{
				// Write plugin ID
				dataOutputStream.writeString(pluginId);

				// Get and write plugin state
				juce::MemoryBlock state = getPluginState(pluginId);
				dataOutputStream.writeInt((int)state.getSize());
				dataOutputStream.write(state.getData(), state.getSize());
			}
        }
        DBG("All plugin states saved successfully to binary file.");
    }
    else
    {
        DBG("Failed to open file for saving plugin states.");
    }

}

void PluginManager::restoreAllPluginStates(const juce::String& dataFilePath)
{
    // Restore plugin states from binary file
    juce::File dataFile(dataFilePath);
    juce::FileInputStream dataInputStream(dataFile);

    if (dataInputStream.openedOk())
    {
        // Read the number of plugins
        int numPlugins = dataInputStream.readInt();
		DBG("Number of plugins to restore: " << numPlugins);

        for (int i = 0; i < numPlugins; ++i)
        {
            // Read plugin ID
            juce::String pluginId = dataInputStream.readString();

            // Read plugin state size and data
            int stateSize = dataInputStream.readInt();
            juce::MemoryBlock state;
            state.setSize(stateSize);
            dataInputStream.read(state.getData(), stateSize);

            // Restore plugin state
            restorePluginState(pluginId, state);
        }
        DBG("All plugin states restored successfully from binary file.");
    }
    else
    {
        DBG("Failed to open file for restoring plugin states.");
    }

}

void PluginManager::renamePluginInstance(const juce::String& oldId, const juce::String& newId)
{
    if (pluginInstances.find(oldId) != pluginInstances.end())
    {
        // Move the plugin instance to the new ID
        pluginInstances[newId] = std::move(pluginInstances[oldId]);
        pluginInstances.erase(oldId);

        // Update the plugin window mapping if necessary
        if (pluginWindows.find(oldId) != pluginWindows.end())
        {
            pluginWindows[newId] = std::move(pluginWindows[oldId]);
            pluginWindows.erase(oldId);
        }

        DBG("Plugin Instance ID renamed from " + oldId + " to " + newId);
    }
    else
    {
        DBG("Error: Plugin Instance ID " + oldId + " not found.");
    }
}

