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
#include <algorithm>

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
    // Remove: deviceManager.initialise(4, 32, nullptr, true); // Remove this duplicate initialization
    setAudioChannels(4, 32); // Keep only this - it properly initializes the inherited AudioDeviceManager
}


PluginManager::~PluginManager()
{
    shutdownAudio();
}

void PluginManager::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    const juce::ScopedLock pluginLock(pluginInstanceLock);
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

    auto& pos = hostPlayHead.positionInfo;

    pos = {};  // clear all flags
    
    // Validate BPM before setting
    if (currentBpm > 0.0)
    {
        pos.setBpm(currentBpm);
    }
    else
    {
        pos.setBpm(120.0); // Default fallback BPM
    }
    
    pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{ 4, 4 });
    
    // Validate sample position
    if (playbackSamplePosition >= 0 && currentSampleRate > 0.0)
    {
        pos.setTimeInSamples(playbackSamplePosition);
        pos.setTimeInSeconds(playbackSamplePosition / currentSampleRate);
        pos.setPpqPosition(playbackSamplePosition
            * (currentBpm / 60.0)
            / currentSampleRate);
    }
    else
    {
        pos.setTimeInSamples(0);
        pos.setTimeInSeconds(0.0);
        pos.setPpqPosition(0.0);
    }
    
    pos.setIsPlaying(true);
    
    // clear the output buffer
    bufferToFill.clearActiveBufferRegion();

    const juce::ScopedLock sl(midiCriticalSection);
    const juce::ScopedLock pluginLock(pluginInstanceLock);

    // Guard against missing audio device
    if (auto* audioDevice = deviceManager.getCurrentAudioDevice(); audioDevice != nullptr)
    {
        double sampleRate = audioDevice->getCurrentSampleRate();

        // Purge MIDI messages for non-existent plugins
        taggedMidiBuffer.erase(
            std::remove_if(taggedMidiBuffer.begin(), taggedMidiBuffer.end(),
                [this](const MyMidiMessage& m)
                {
                    return pluginInstances.find(m.pluginId) == pluginInstances.end();
                }),
            taggedMidiBuffer.end());

        // Cap buffer growth
        const std::size_t maxBufferSize = 50000; // maximum number of MIDI messages to keep
        if (taggedMidiBuffer.size() > maxBufferSize)
            taggedMidiBuffer.erase(taggedMidiBuffer.begin(),
                taggedMidiBuffer.begin() + (taggedMidiBuffer.size() - maxBufferSize));

        const bool isStartingPlayback = (playbackSamplePosition == 0);
        const int graceWindow = bufferToFill.numSamples;
        std::unordered_map<juce::String, juce::MidiBuffer> scheduledPluginMessages;

        if (!taggedMidiBuffer.empty())
        {
            std::vector<MyMidiMessage> remainingMessages;
            remainingMessages.reserve(taggedMidiBuffer.size());

            for (auto& taggedMessage : taggedMidiBuffer)
            {
                if (pluginInstances.find(taggedMessage.pluginId) == pluginInstances.end())
                    continue;

                bool consumed = false;

                if (sampleRate <= 0.0 || taggedMessage.timestamp == 0)
                {
                    scheduledPluginMessages[taggedMessage.pluginId].addEvent(taggedMessage.message, 0);
                    consumed = true;
                }
                else
                {
                    auto absPos = static_cast<juce::int64>((taggedMessage.timestamp / 1000.0) * sampleRate);
                    auto offset64 = absPos - playbackSamplePosition;
                    const int offset = static_cast<int>(offset64);

                    if ((isStartingPlayback && offset >= -graceWindow && offset < bufferToFill.numSamples)
                        || (offset >= 0 && offset < bufferToFill.numSamples))
                    {
                        scheduledPluginMessages[taggedMessage.pluginId].addEvent(
                            taggedMessage.message,
                            juce::jmax(0, offset));
                        consumed = true;
                    }
                }

                if (!consumed)
                    remainingMessages.push_back(std::move(taggedMessage));
            }

            taggedMidiBuffer.swap(remainingMessages);
        }

        // 2) Process each plugin once, in a single loop
        for (auto& [pluginId, pluginInstance] : pluginInstances)
        {
            if (!pluginInstance)
                continue;

            // Validate plugin instance before processing
            try
            {
                // Check if plugin is properly initialized
                if (pluginInstance->getTotalNumOutputChannels() <= 0)
                {
                    DBG("Warning: Plugin " << pluginId << " has no output channels, skipping");
                    continue;
                }

                // d) prepare a per-plugin tempBuffer with correct channel count
                int numOut = pluginInstance->getTotalNumOutputChannels();
                juce::AudioBuffer<float> tempBuffer(numOut, bufferToFill.numSamples);
                tempBuffer.clear();

                // e) gather tagged MIDI for this plugin
                juce::MidiBuffer matchingMessages;
                if (auto scheduled = scheduledPluginMessages.find(pluginId);
                    scheduled != scheduledPluginMessages.end())
                {
                    matchingMessages = std::move(scheduled->second);
                }

                // merge in live incoming MIDI if this is the selected plugin
                if (pluginId == mainComponent->getOrchestraTableModel().getSelectedPluginId())
                    matchingMessages.addEvents(incomingMidi,
                        0,
                        bufferToFill.numSamples,
                        bufferToFill.startSample);

                // f) run the plugin with error handling
                try
                {
                    pluginInstance->processBlock(tempBuffer, matchingMessages);
                }
                catch (const std::exception& e)
                {
                    DBG("Exception processing plugin " << pluginId << ": " << e.what());
                    tempBuffer.clear(); // Clear buffer to avoid audio artifacts
                    continue;
                }
                catch (...)
                {
                    DBG("Unknown exception processing plugin " << pluginId);
                    tempBuffer.clear(); // Clear buffer to avoid audio artifacts
                    continue;
                }

                // g) mix plugin output back into the host buffer
                for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
                {
                    int outCh = ch < tempBuffer.getNumChannels() ? ch : tempBuffer.getNumChannels() - 1;
                    if (outCh >= 0 && outCh < tempBuffer.getNumChannels())
                    {
                        bufferToFill.buffer->addFrom(ch,
                            bufferToFill.startSample,
                            tempBuffer,
                            outCh,
                            0,
                            bufferToFill.numSamples);
                    }
                }
            }
            catch (const std::exception& e)
            {
                DBG("Exception in plugin processing loop for " << pluginId << ": " << e.what());
                continue;
            }
            catch (...)
            {
                DBG("Unknown exception in plugin processing loop for " << pluginId);
                continue;
            }
        }
    }
    else
    {
        // If the device is missing, clear buffers and skip processing
        bufferToFill.clearActiveBufferRegion();
        incomingMidi.clear();
        return;
    }

    // TAP audio for UDP streaming
    // Tap the final mixed audio buffer once per callback
    if (audioTapCallback != nullptr)
    {
        try
        {
            audioTapCallback(*bufferToFill.buffer);
        }
        catch (const std::exception& e)
        {
            DBG("Exception in audio tap callback: " << e.what());
        }
        catch (...)
        {
            DBG("Unknown exception in audio tap callback");
        }
    }

    // clear incoming MIDI and advance the host clock
    incomingMidi.clear();
    playbackSamplePosition += bufferToFill.numSamples;
}

void PluginManager::releaseResources()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
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
                const juce::ScopedLock pluginLock(pluginInstanceLock);
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
    const juce::ScopedLock pluginLock(pluginInstanceLock);
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
        const juce::ScopedLock pluginLock(pluginInstanceLock);
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
        const juce::ScopedLock pluginLock(pluginInstanceLock);
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

        const juce::ScopedLock pluginLock(pluginInstanceLock);
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
        const juce::ScopedLock pluginLock(pluginInstanceLock);
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
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        // Destroy the plugin window first which also deletes the editor
        pluginWindows.erase(pluginId);

        // Release and reset the plugin instance
        pluginInstances[pluginId]->releaseResources();
        pluginInstances[pluginId].reset();

        // Remove instance entry from map
        pluginInstances.erase(pluginId);

        DBG("Plugin reset: " << pluginId);
    }
}

void PluginManager::resetAllPlugins()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
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
        const juce::ScopedLock pluginLock(pluginInstanceLock);
        if (pluginInstances.find(pluginId) != pluginInstances.end())
        {
                return true;
        }
    return false;
}

void PluginManager::listPluginInstances()
{
        const juce::ScopedLock pluginLock(pluginInstanceLock);
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
                // Validate that the plugin exists before attempting to use it. Using
                // operator[] here would create a new empty entry and give us a null
                // pointer, so explicitly look up the plugin first.
                const juce::ScopedLock pluginLock(pluginInstanceLock);
                auto pluginIt = pluginInstances.find(pluginId);
                if (pluginIt == pluginInstances.end() || pluginIt->second == nullptr)
                {
                        DBG("Failed to save plugin data. Plugin not found: " << pluginId);
                        return;
                }

                // Create VST3Visitor instance
                CustomVST3Visitor visitor;

                // Get the plugin instance
                juce::AudioPluginInstance* plugin = pluginIt->second.get();

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
    const juce::ScopedLock pluginLock(pluginInstanceLock);
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
        if (knownPluginList.getNumTypes() > 50)
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
    // Create DawServer subfolder in user's documents directory
    juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DawServer");
    if (!dawServerDir.exists())
        dawServerDir.createDirectory();

    // Use DawServer subfolder for PluginList.xml
    juce::File pluginListFile = dawServerDir.getChildFile("PluginList.xml");

    std::unique_ptr<juce::XmlElement> pluginListXml = knownPluginList.createXml();

    if (pluginListXml != nullptr)
    {
        if (pluginListFile.existsAsFile())
            pluginListFile.deleteFile();

        bool existed = pluginListFile.existsAsFile();
        pluginListXml->writeTo(pluginListFile);

        if (!existed)
            pluginListFile.setCreationTime(juce::Time::getCurrentTime());

        pluginListFile.setLastModificationTime(juce::Time::getCurrentTime());
        pluginListFile.setLastAccessTime(juce::Time::getCurrentTime());
    }
    else
    {
        DBG("Failed to create XML from plugin list");
    }
}

bool PluginManager::loadPluginListFromFile()
{
    // Create DawServer subfolder in user's documents directory
    juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DawServer");
    if (!dawServerDir.exists())
        dawServerDir.createDirectory();

    // Use DawServer subfolder for PluginList.xml
    juce::File pluginListFile = dawServerDir.getChildFile("PluginList.xml");

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
            DBG("Failed to parse XML from PluginList.xml");
            return false;
        }
    }
    else
    {
        DBG("PluginList.xml does not exist");
        return false;
    }
}

void PluginManager::clearTaggedMidiBuffer()
{
    const juce::ScopedLock sl(midiCriticalSection);
    taggedMidiBuffer.clear();
}

void PluginManager::printTaggedMidiBuffer()
{
	const juce::ScopedLock sl(midiCriticalSection);
	DBG("Tagged MIDI Buffer Contents:");
	for (const auto& taggedMessage : taggedMidiBuffer)
	{
		DBG("Plugin ID: " << taggedMessage.pluginId
			<< ", Timestamp: " << taggedMessage.timestamp
			<< ", Message: " << taggedMessage.message.getDescription());
	}
}

void PluginManager::addMidiMessage(const juce::MidiMessage& message, const juce::String& pluginId, juce::int64& adjustedTimestamp)
{
    const juce::ScopedLock sl(midiCriticalSection); // Lock the critical section to ensure thread safety
    // Add the tagged MIDI message to the buffer with a timestamp
    
    taggedMidiBuffer.emplace_back(message, pluginId, adjustedTimestamp);
	// DBG("Added MIDI message: " << message.getDescription() << " for pluginId: " << pluginId << " at adjusted time: " << juce::String(adjustedTimestamp));
}

void PluginManager::resetPlayback()
{
        playbackSamplePosition = 0;
        hostPlayHead.positionInfo.setIsPlaying(false);
        // Also clear the taggedMidiBuffer under the MIDI lock
        const juce::ScopedLock sl(midiCriticalSection);
        taggedMidiBuffer.clear();

}

// And stop any currently playing notes
void PluginManager::stopAllNotes()
{
    const juce::ScopedLock sl(midiCriticalSection);
    for (auto& [pluginId, pluginInstance] : pluginInstances)
    {
        if (pluginInstance != nullptr)
        {
            int numOut = pluginInstance->getTotalNumOutputChannels();
            if (numOut <= 0)
                continue; // Skip plugins with no output channels

            juce::MidiBuffer stopMessages;
            for (int channel = 1; channel <= 16; ++channel)
            {
                stopMessages.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                stopMessages.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
            }
            juce::AudioBuffer<float> dummyBuffer(numOut, 512);
            dummyBuffer.clear();

            try
            {
                pluginInstance->processBlock(dummyBuffer, stopMessages);
            }
            catch (const std::exception& e)
            {
                DBG("Exception in stopAllNotes for plugin " << pluginId << ": " << e.what());
            }
            catch (...)
            {
                DBG("Unknown exception in stopAllNotes for plugin " << pluginId);
            }
        }
    }
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
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        pluginInstances[pluginId]->getStateInformation(state);
    }
    else
	{
		DBG("Plugin not found: " << pluginId);
	}
    return state;
}


void PluginManager::restorePluginState(const juce::String& pluginId, const juce::MemoryBlock& state)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
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
        const juce::ScopedLock pluginLock(pluginInstanceLock);
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
            DBG("Restoring state for plugin: " << pluginId);
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
    const juce::ScopedLock pluginLock(pluginInstanceLock);
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

// Ensure deviceManager is properly initialized and not set to "No Device"
// If you have any code that sets outputDeviceName to "No Device", remove or comment it out.

// ...existing code...

