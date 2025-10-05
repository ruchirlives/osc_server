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
    initialiseGraph();
    graph.setPlayHead(&hostPlayHead);
}


PluginManager::~PluginManager()
{
    shutdownAudio();
}

void PluginManager::initialiseGraph()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    graph.clear();
    pluginNodeIds.clear();
    pluginOrder.clear();
    pluginMidiChannels.clear();
    nextMidiChannel = 1;

    audioInputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    audioOutputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    midiInputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    midiOutputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiOutputNode));

    rebuildGraphConnections();

    if (auto* audioDevice = deviceManager.getCurrentAudioDevice(); audioDevice != nullptr)
        graph.prepareToPlay(audioDevice->getCurrentSampleRate(), audioDevice->getCurrentBufferSizeSamples());
}

void PluginManager::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    graph.setPlayHead(&hostPlayHead);
    graph.prepareToPlay(sampleRate, samplesPerBlockExpected);
}

void PluginManager::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto& pos = hostPlayHead.positionInfo;

    pos = {};  // clear all flags

    if (currentBpm > 0.0)
        pos.setBpm(currentBpm);
    else
        pos.setBpm(120.0);

    pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{ 4, 4 });

    if (playbackSamplePosition >= 0 && currentSampleRate > 0.0)
    {
        pos.setTimeInSamples(playbackSamplePosition);
        pos.setTimeInSeconds(playbackSamplePosition / currentSampleRate);
        pos.setPpqPosition(playbackSamplePosition * (currentBpm / 60.0) / currentSampleRate);
    }
    else
    {
        pos.setTimeInSamples(0);
        pos.setTimeInSeconds(0.0);
        pos.setPpqPosition(0.0);
    }

    pos.setIsPlaying(true);

    bufferToFill.clearActiveBufferRegion();

    juce::MidiBuffer midiToProcess;
    bool deviceMissing = false;

    {
        const juce::ScopedLock midiLock(midiCriticalSection);
        const juce::ScopedLock pluginLock(pluginInstanceLock);

        if (auto* audioDevice = deviceManager.getCurrentAudioDevice(); audioDevice != nullptr)
        {
            double sampleRate = audioDevice->getCurrentSampleRate();

            taggedMidiBuffer.erase(
                std::remove_if(taggedMidiBuffer.begin(), taggedMidiBuffer.end(),
                    [this](const MyMidiMessage& m)
                    {
                        return pluginNodeIds.find(m.pluginId) == pluginNodeIds.end();
                    }),
                taggedMidiBuffer.end());

            const std::size_t maxBufferSize = 1024;
            if (taggedMidiBuffer.size() > maxBufferSize)
                taggedMidiBuffer.erase(taggedMidiBuffer.begin(),
                    taggedMidiBuffer.begin() + (taggedMidiBuffer.size() - maxBufferSize));

            bool isStartingPlayback = (playbackSamplePosition == 0);
            const int graceWindow = bufferToFill.numSamples;

            for (auto it = taggedMidiBuffer.begin(); it != taggedMidiBuffer.end();)
            {
                const auto& tm = *it;
                int offset = 0;

                if (tm.timestamp == 0)
                {
                    auto message = tm.message;
                    if (auto channelIt = pluginMidiChannels.find(tm.pluginId); channelIt != pluginMidiChannels.end() && message.isChannelMessage())
                        message.setChannel(channelIt->second);

                    midiToProcess.addEvent(message, 0);
                    it = taggedMidiBuffer.erase(it);
                    continue;
                }

                auto absPos = static_cast<juce::int64>((tm.timestamp / 1000.0) * sampleRate);
                offset = static_cast<int>(absPos - playbackSamplePosition);

                if ((isStartingPlayback && offset >= -graceWindow && offset < bufferToFill.numSamples)
                    || (offset >= 0 && offset < bufferToFill.numSamples))
                {
                    auto message = tm.message;
                    if (auto channelIt = pluginMidiChannels.find(tm.pluginId); channelIt != pluginMidiChannels.end() && message.isChannelMessage())
                        message.setChannel(channelIt->second);

                    midiToProcess.addEvent(message, juce::jmax(0, offset));
                    it = taggedMidiBuffer.erase(it);
                    continue;
                }

                ++it;
            }

            midiToProcess.addEvents(incomingMidi,
                0,
                bufferToFill.numSamples,
                bufferToFill.startSample);
        }
        else
        {
            bufferToFill.clearActiveBufferRegion();
            incomingMidi.clear();
            taggedMidiBuffer.clear();
            deviceMissing = true;
        }
    }

    if (deviceMissing)
        return;

    {
        const juce::ScopedLock pluginLock(pluginInstanceLock);
        graph.processBlock(*bufferToFill.buffer, midiToProcess);
    }

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

    incomingMidi.clear();
    playbackSamplePosition += bufferToFill.numSamples;
}

void PluginManager::releaseResources()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    graph.releaseResources();
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
    for (const auto& pluginPair : pluginNodeIds)
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
        instance->setPlayHead(&hostPlayHead);
        instance->prepareToPlay(sampleRate, blockSize);

        const juce::ScopedLock pluginLock(pluginInstanceLock);
        if (auto node = graph.addNode(std::move(instance)))
        {
            pluginNodeIds[pluginId] = node->nodeID;
            pluginOrder.push_back(pluginId);

            int assignedChannel = -1;
            for (int candidate = 1; candidate <= 16; ++candidate)
            {
                const bool channelInUse = std::any_of(pluginMidiChannels.begin(), pluginMidiChannels.end(),
                    [candidate](const auto& pair)
                    {
                        return pair.second == candidate;
                    });

                if (!channelInUse)
                {
                    assignedChannel = candidate;
                    break;
                }
            }

            if (assignedChannel == -1)
            {
                assignedChannel = nextMidiChannel;
                nextMidiChannel = (nextMidiChannel % 16) + 1;
            }
            else
            {
                nextMidiChannel = (assignedChannel % 16) + 1;
            }

            pluginMidiChannels[pluginId] = assignedChannel;

            rebuildGraphConnections();
            DBG("Plugin instantiated successfully: " << pluginId);
        }
        else
        {
            DBG("Failed to add plugin node to graph: " << pluginId);
        }
    }
    else
    {
        DBG("Error instantiating plugin: " << errorMessage);
    }
}

void PluginManager::openPluginWindow(juce::String pluginId)
{
    if (auto* pluginInstance = getPluginInstance(pluginId))
    {
        {
            const juce::ScopedLock pluginLock(pluginInstanceLock);
            auto windowIt = pluginWindows.find(pluginId);
            if (windowIt == pluginWindows.end())
            {
                pluginWindows[pluginId] = std::make_unique<PluginWindow>(pluginInstance);
            }
            else
            {
                windowIt->second->setVisible(true);
            }
        }

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

    if (!hasPluginInstance(pluginId))
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
    if (auto* pluginInstance = getPluginInstance(pluginId))
    {
        juce::PluginDescription desc = pluginInstance->getPluginDescription();
        response = desc.name;
        DBG("Plugin data found: " << response);
    }
    return response;
}

void PluginManager::resetPlugin(const juce::String& pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    auto nodeIt = pluginNodeIds.find(pluginId);
    if (nodeIt == pluginNodeIds.end())
        return;

    pluginWindows.erase(pluginId);
    graph.removeNode(nodeIt->second);
    pluginNodeIds.erase(nodeIt);
    pluginMidiChannels.erase(pluginId);
    pluginOrder.erase(std::remove(pluginOrder.begin(), pluginOrder.end(), pluginId), pluginOrder.end());

    rebuildGraphConnections();
    DBG("Plugin reset: " << pluginId);

    {
        const juce::ScopedLock midiLock(midiCriticalSection);
        taggedMidiBuffer.erase(
            std::remove_if(taggedMidiBuffer.begin(), taggedMidiBuffer.end(),
                [&pluginId](const MyMidiMessage& message)
                {
                    return message.pluginId == pluginId;
                }),
            taggedMidiBuffer.end());
    }
}

void PluginManager::resetAllPlugins()
{
    {
        const juce::ScopedLock pluginLock(pluginInstanceLock);
        pluginWindows.clear();
    }

    {
        const juce::ScopedLock midiLock(midiCriticalSection);
        taggedMidiBuffer.clear();
    }

    initialiseGraph();
    DBG("All plugins have been reset.");
}



bool PluginManager::hasPluginInstance(const juce::String& pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    return pluginNodeIds.find(pluginId) != pluginNodeIds.end();
}

void PluginManager::listPluginInstances()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    for (const auto& [pluginId, nodeId] : pluginNodeIds)
    {
        DBG("Plugin ID: " << pluginId);
        if (auto node = graph.getNodeForId(nodeId))
        {
            if (auto* plugin = dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor()))
                DBG("Plugin Name: " << plugin->getName());
        }
    }
}

void PluginManager::savePluginData(const juce::String& dataFilePath, const juce::String& filename, const juce::String& pluginId)
{
        // Get plugin unique ID
        juce::String uniqueId = getPluginUniqueId(pluginId);
        juce::ignoreUnused(uniqueId);

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
                auto* plugin = getPluginInstance(pluginId);
                if (plugin == nullptr)
                {
                        DBG("Failed to save plugin data. Plugin not found: " << pluginId);
                        return;
                }

                CustomVST3Visitor visitor;
                plugin->getExtensions(visitor);

                juce::MemoryBlock state = visitor.presetData;

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
    auto* plugin = getPluginInstance(pluginId);
    if (plugin == nullptr)
    {
        DBG("Error: Plugin ID not found or plugin instance is null.");
        return "Invalid Plugin ID";
    }

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
	DBG("Added MIDI message: " << message.getDescription() << " for pluginId: " << pluginId << " at adjusted time: " << juce::String(adjustedTimestamp));
}

void PluginManager::resetPlayback()
{
        playbackSamplePosition = 0;
        hostPlayHead.positionInfo.setIsPlaying(false);
        // Also clear the taggedMidiBuffer
        taggedMidiBuffer.clear();

}

// And stop any currently playing notes
void PluginManager::stopAllNotes()
{
    const juce::ScopedLock midiLock(midiCriticalSection);
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    for (const auto& [pluginId, channel] : pluginMidiChannels)
    {
        auto allNotesOff = juce::MidiMessage::allNotesOff(channel);
        auto allSoundOff = juce::MidiMessage::allSoundOff(channel);
        taggedMidiBuffer.emplace_back(allNotesOff, pluginId, 0);
        taggedMidiBuffer.emplace_back(allSoundOff, pluginId, 0);
    }
}



juce::int8 PluginManager::getNumInstances(std::vector<juce::String>& instances)
{
    juce::int8 numInstances = 0;
    const juce::ScopedLock pluginLock(pluginInstanceLock);

    if (instances.empty())
    {
        numInstances = static_cast<juce::int8>(pluginNodeIds.size());
        DBG("Number of total plugins if selection not used: " << juce::String(numInstances));
    }
    else
    {
        for (const auto& instance : instances)
        {
            if (pluginNodeIds.find(instance) != pluginNodeIds.end())
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
                const juce::ScopedLock pluginLock(pluginInstanceLock);
                for (const auto& pluginPair : pluginNodeIds)
                {
                        if (instances.empty() || std::find(instances.begin(), instances.end(), pluginPair.first) != instances.end())
                        {
                                dataOutputStream.writeString(pluginPair.first);

                                if (auto node = graph.getNodeForId(pluginPair.second))
                                {
                                        if (auto* plugin = dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor()))
                                        {
                                                juce::PluginDescription desc = plugin->getPluginDescription();
                                                dataOutputStream.writeString(desc.name);
                                        }
                                        else
                                        {
                                                dataOutputStream.writeString({});
                                        }
                                }
                                else
                                {
                                        dataOutputStream.writeString({});
                                }
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

            // first check if the pluginId is not already in the graph
                        if (pluginNodeIds.find(pluginId) == pluginNodeIds.end())
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
    if (auto* plugin = getPluginInstance(pluginId))
    {
        plugin->getStateInformation(state);
    }
    else
    {
        DBG("Plugin not found: " << pluginId);
    }
    return state;
}


void PluginManager::restorePluginState(const juce::String& pluginId, const juce::MemoryBlock& state)
{
    if (auto* plugin = getPluginInstance(pluginId))
    {
        plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
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
        

        std::vector<juce::String> idsToSave;
        {
            const juce::ScopedLock pluginLock(pluginInstanceLock);
            for (const auto& [pluginId, _] : pluginNodeIds)
            {
                if (instances.empty() || std::find(instances.begin(), instances.end(), pluginId) != instances.end())
                    idsToSave.push_back(pluginId);
            }
        }

        for (const auto& pluginId : idsToSave)
        {
            dataOutputStream.writeString(pluginId);

            juce::MemoryBlock state = getPluginState(pluginId);
            dataOutputStream.writeInt((int)state.getSize());
            dataOutputStream.write(state.getData(), state.getSize());
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
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    auto oldNode = pluginNodeIds.find(oldId);
    if (oldNode == pluginNodeIds.end())
    {
        DBG("Error: Plugin Instance ID " + oldId + " not found.");
        return;
    }

    if (pluginNodeIds.find(newId) != pluginNodeIds.end())
    {
        DBG("Error: Plugin Instance ID " + newId + " already exists.");
        return;
    }

    auto nodeId = oldNode->second;
    pluginNodeIds.erase(oldNode);
    pluginNodeIds[newId] = nodeId;

    if (auto orderIt = std::find(pluginOrder.begin(), pluginOrder.end(), oldId); orderIt != pluginOrder.end())
        *orderIt = newId;

    if (auto midiIt = pluginMidiChannels.find(oldId); midiIt != pluginMidiChannels.end())
    {
        int channel = midiIt->second;
        pluginMidiChannels.erase(midiIt);
        pluginMidiChannels[newId] = channel;
    }

    if (auto windowIt = pluginWindows.find(oldId); windowIt != pluginWindows.end())
    {
        pluginWindows[newId] = std::move(windowIt->second);
        pluginWindows.erase(oldId);
    }

    for (auto& taggedMessage : taggedMidiBuffer)
    {
        if (taggedMessage.pluginId == oldId)
            taggedMessage.pluginId = newId;
    }

    DBG("Plugin Instance ID renamed from " + oldId + " to " + newId);
}

void PluginManager::rebuildGraphConnections()
{
    if (audioInputNode == nullptr || audioOutputNode == nullptr)
        return;

    graph.clearConnections();

    auto connectAudio = [this](juce::AudioProcessorGraph::NodeID sourceId, juce::AudioProcessorGraph::NodeID destId)
    {
        if (sourceId == destId)
            return;

        if (auto sourceNode = graph.getNodeForId(sourceId))
        {
            if (auto destNode = graph.getNodeForId(destId))
            {
                auto sourceOutputs = sourceNode->getProcessor()->getTotalNumOutputChannels();
                auto destInputs = destNode->getProcessor()->getTotalNumInputChannels();
                auto numChannels = juce::jmin(sourceOutputs, destInputs);

                for (int ch = 0; ch < numChannels; ++ch)
                    graph.addConnection({ { sourceId, ch }, { destId, ch } });
            }
        }
    };

    juce::AudioProcessorGraph::NodeID lastNodeId = audioInputNode->nodeID;

    if (pluginOrder.empty())
    {
        connectAudio(audioInputNode->nodeID, audioOutputNode->nodeID);
        if (midiInputNode != nullptr && midiOutputNode != nullptr)
            graph.addConnection({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }, { midiOutputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });
    }
    else
    {
        for (const auto& pluginId : pluginOrder)
        {
            auto nodeIt = pluginNodeIds.find(pluginId);
            if (nodeIt == pluginNodeIds.end())
                continue;

            auto nodeId = nodeIt->second;
            connectAudio(lastNodeId, nodeId);

            if (midiInputNode != nullptr)
                graph.addConnection({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex }, { nodeId, juce::AudioProcessorGraph::midiChannelIndex } });

            if (midiOutputNode != nullptr)
                graph.addConnection({ { nodeId, juce::AudioProcessorGraph::midiChannelIndex }, { midiOutputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });

            lastNodeId = nodeId;
        }

        connectAudio(lastNodeId, audioOutputNode->nodeID);
    }
}

juce::AudioPluginInstance* PluginManager::getPluginInstance(const juce::String& pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    auto nodeIt = pluginNodeIds.find(pluginId);
    if (nodeIt == pluginNodeIds.end())
        return nullptr;

    if (auto node = graph.getNodeForId(nodeIt->second))
        return dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor());

    return nullptr;
}

// Ensure deviceManager is properly initialized and not set to "No Device"
// If you have any code that sets outputDeviceName to "No Device", remove or comment it out.

// ...existing code...

