/*
  ==============================================================================

    MidiManager.cpp
    Created: 16 Sep 2024 4:18:39pm
    Author:  Desktop

  ==============================================================================
*/

#include "MidiManager.h"
#include "MainComponent.h"
#include "PluginManager.h"

#include <limits>
#include <map>
#include <utility>
#include <algorithm>


void MidiManager::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
	// DBG("MIDI Message Received: " + message.getDescription() + " from " + source->getName());
	// Forward the MIDI message to the buffee is their is a plugin instance

    // playOverdubOnTriggerArmed
        if (playOverdubOnTriggerArmed)
        {
        // Acquire the MessageManagerLock before calling any JUCE UI/component method
        juce::MessageManagerLock lock;
        if (!lock.lockWasGained())
            return; // Could not get the lock, so do not call UI code

                playOverdubOnTriggerArmed = false;
        startOverdub(false);
                mainComponent->updateOverdubUI();

        }
	const juce::ScopedLock sl(midiCriticalSection); // Use the custom CriticalSection for thread safety

	// Always record MIDI events while the plugin is active
	if (midiInput != nullptr)
	{

		// Use high-resolution ticks for accurate MIDI event timing
		juce::int64 currentTimeTicks = juce::Time::getHighResolutionTicks()-recordStartTime;
		DBG("Current Time Ticks: " + juce::String(currentTimeTicks));

		// Get the current midi channel from selected row in the table from MainComponent
		int midiChannel = mainComponent->getOrchestraTableModel().getSelectedMidiChannel(); // Get the selected MIDI channel from the table

		DBG("MIDI Channel: " + juce::String(midiChannel));

		// Create a new MIDI message with the selected MIDI channel
		juce::MidiMessage messageWithChannel = message;
		messageWithChannel.setChannel(midiChannel);
		messageWithChannel.setTimeStamp(static_cast<double>(currentTimeTicks));
		incomingMidi.addEvent(messageWithChannel, 0);


		// Stamp the MIDI message with high-resolution ticks directly
		if (isOverdubbing)
		{
			recordBuffer.addEvent(messageWithChannel, static_cast<int>(currentTimeTicks));
		}
	}
}

std::map<int, juce::String> MidiManager::buildChannelTagMap(const juce::String& pluginId) const
{
        std::map<int, juce::String> channelTags;

        if (mainComponent == nullptr)
                return channelTags;

        const auto& orchestra = mainComponent->getConductor().orchestra;
        for (const auto& instrument : orchestra)
        {
                if (pluginId.isNotEmpty() && instrument.pluginInstanceId != pluginId)
                        continue;

                auto tagString = serialiseTags(instrument.tags);
                if (tagString.isEmpty())
                        tagString = instrument.instrumentName;

                channelTags[instrument.midiChannel] = tagString;
        }

        return channelTags;
}

juce::String MidiManager::serialiseTags(const std::vector<juce::String>& tags)
{
        juce::StringArray tagArray;
        for (const auto& tag : tags)
        {
                auto trimmed = tag.trim();
                if (trimmed.isNotEmpty())
                        tagArray.add(trimmed);
        }

        return tagArray.joinIntoString(", ");
}

juce::String MidiManager::extractTrackName(const juce::MidiMessageSequence& sequence)
{
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
                const auto* holder = sequence.getEventPointer(i);
                if (holder == nullptr)
                        continue;

                const auto& message = holder->message;
                if (message.isTextMetaEvent() && message.getMetaEventType() == 0x03)
                        return message.getTextFromTextMetaEvent();
        }

        return {};
}

std::map<int, MidiManager::ChannelTrackInfo> MidiManager::buildChannelSequences(const juce::MidiBuffer& bufferCopy) const
{
        std::map<int, ChannelTrackInfo> channelSequences;

        if (bufferCopy.getNumEvents() == 0)
                return channelSequences;

        struct EventData
        {
                juce::MidiMessage message;
                int channel{ 0 };
                juce::int64 timestamp{ 0 };
        };

        std::vector<EventData> events;
        events.reserve(static_cast<size_t>(bufferCopy.getNumEvents()));

        juce::int64 earliestTimestamp = std::numeric_limits<juce::int64>::max();

        juce::MidiBuffer::Iterator iterator(bufferCopy);
        juce::MidiMessage eventMessage;
        int samplePosition = 0;

        while (iterator.getNextEvent(eventMessage, samplePosition))
        {
                EventData data;
                data.message = eventMessage;
                data.channel = juce::jlimit(1, 16, data.message.getChannel());
                data.timestamp = getTimestampFromEvent(data.message, samplePosition);

                if (data.timestamp < earliestTimestamp)
                        earliestTimestamp = data.timestamp;

                events.push_back(std::move(data));
        }

        if (earliestTimestamp == std::numeric_limits<juce::int64>::max())
                earliestTimestamp = 0;

        auto ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();
        double ticksPerQuarterNote = 960.0;
        double bpm = mainComponent != nullptr ? mainComponent->getBpm() : 120.0;
        if (bpm <= 0.0)
                bpm = 120.0;

        double tickConversionFactor = ticksPerSecond > 0
                ? (ticksPerQuarterNote * bpm) / (static_cast<double>(ticksPerSecond) * 60.0)
                : 1.0;

        for (auto& event : events)
        {
                auto timeDiff = event.timestamp - earliestTimestamp;
                if (timeDiff < 0)
                        timeDiff = 0;

                double timeInTicks = static_cast<double>(timeDiff) * tickConversionFactor;
                if (timeInTicks < 0.0)
                        timeInTicks = 0.0;

                event.message.setTimeStamp(timeInTicks);

                auto& channelInfo = channelSequences[event.channel];
                channelInfo.sequence.addEvent(event.message);
        }

        return channelSequences;
}

void MidiManager::writeMidiFile(const juce::File& file, const std::map<int, ChannelTrackInfo>& channelSequences) const
{
        if (channelSequences.empty())
                return;

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(960);

        for (const auto& entry : channelSequences)
        {
                auto sequence = entry.second.sequence;
                if (sequence.getNumEvents() == 0)
                        continue;

                if (entry.second.trackName.isNotEmpty())
                {
                        auto trackNameMessage = juce::MidiMessage::textMetaEvent(0x03, entry.second.trackName);
                        trackNameMessage.setTimeStamp(0.0);
                        sequence.addEvent(trackNameMessage);
                }

                sequence.updateMatchedPairs();
                midiFile.addTrack(sequence);
        }

        if (midiFile.getNumTracks() == 0)
                return;

        juce::File parentDir = file.getParentDirectory();
        if (!parentDir.exists())
                parentDir.createDirectory();

        if (file.existsAsFile())
                file.deleteFile();

        juce::FileOutputStream stream(file);
        if (!stream.openedOk())
        {
                DBG("Failed to open file for writing MIDI data: " + file.getFullPathName());
                return;
        }

        midiFile.writeTo(stream);
        stream.flush();
        DBG("MIDI File Saved: " + file.getFullPathName());
}

void MidiManager::openMidiInput(juce::String midiInputName)
{
	closeMidiInput();

	// iterate through the available MIDI input devices
	auto midiInputs = juce::MidiInput::getAvailableDevices();
	
	for (auto input : midiInputs)
	{
		if (input.name == midiInputName)
		{
			midiInput = juce::MidiInput::openDevice(input.identifier, this);
			if (midiInput)
			{
				midiInput->start();
				DBG("MIDI Input Opened: " + midiInputName);
			}
		}
	}

}

void MidiManager::closeMidiInput()
{
	// Close the MIDI input device
	if (midiInput != nullptr)
	{
		// Stop MIDI input
		midiInput->stop();

		// Retrieve device identifier before resetting
		juce::String deviceIdentifier = midiInput->getIdentifier();

		// Reset midiInput to release resources
		midiInput.reset();
		midiInput = nullptr;
		DBG("Using WinRT MIDI: " << JUCE_USE_WINRT_MIDI);

	}
}


void MidiManager::startOverdub(bool stopActiveNotes)
{
        juce::MidiBuffer bufferCopy;
        {
                const juce::ScopedLock sl(midiCriticalSection);
        if (stopActiveNotes)
            mainComponent->getPluginManager().stopAllNotes();
                overdubHistory.emplace_back(recordBuffer);
                isOverdubbing = true;
                // Do NOT clear recordBuffer!
                recordStartTime = juce::Time::getHighResolutionTicks();
                bufferCopy = recordBuffer;
	}

	republishRecordedEvents(bufferCopy);
}

// Then call this from MidiManager::stopOverdub():
void MidiManager::stopOverdub()
{
    const juce::ScopedLock sl(midiCriticalSection);
	mainComponent->getPluginManager().stopAllNotes();
    isOverdubbing = false;
	mainComponent->getPluginManager().clearTaggedMidiBuffer();
    // stopAllNotes

}


void MidiManager::triggerOverdub()
{
    if (isOverdubbing)
    {
        playOverdubOnTriggerArmed = false;
        stopOverdub();
    }
    else
    {
        // Toggle the armed state
        playOverdubOnTriggerArmed = !playOverdubOnTriggerArmed;
    }
}

void MidiManager::playOverdub()
{
    juce::MidiBuffer bufferCopy;
    {
        const juce::ScopedLock sl(midiCriticalSection);
        mainComponent->getPluginManager().stopAllNotes();
        //overdubHistory.emplace_back(recordBuffer);
        isOverdubbing = false;
        // Do NOT clear recordBuffer!
        recordStartTime = juce::Time::getHighResolutionTicks();
        bufferCopy = recordBuffer;
    }

    republishRecordedEvents(bufferCopy);
}

void MidiManager::stripLeadingSilence()
{
        juce::MidiBuffer bufferCopy;
        {
                const juce::ScopedLock sl(midiCriticalSection);

                if (recordBuffer.getNumEvents() == 0)
                        return;

                juce::int64 earliestTimestamp = std::numeric_limits<juce::int64>::max();
                juce::MidiBuffer::Iterator it(recordBuffer);
                juce::MidiMessage msg;
                int samplePosition;

                while (it.getNextEvent(msg, samplePosition))
                {
                        auto ts = getTimestampFromEvent(msg, samplePosition);
                        if (ts < earliestTimestamp)
                                earliestTimestamp = ts;
                }

                if (earliestTimestamp <= 0 || earliestTimestamp == std::numeric_limits<juce::int64>::max())
                        return;

                juce::MidiBuffer adjustedBuffer;
                juce::MidiBuffer::Iterator it2(recordBuffer);

                while (it2.getNextEvent(msg, samplePosition))
                {
                        auto ts = getTimestampFromEvent(msg, samplePosition);
                        juce::MidiMessage adjusted = msg;
						auto shifted = ts - earliestTimestamp;
						if (shifted < 0)
							shifted = 0;
						adjusted.setTimeStamp(static_cast<double>(shifted));
						adjustedBuffer.addEvent(adjusted, static_cast<int>(shifted));

                }

                recordBuffer = adjustedBuffer;
                bufferCopy = recordBuffer;
        }
		isStripped = true;

        //republishRecordedEvents(bufferCopy);
}

void MidiManager::undoLastOverdub()
{
        juce::MidiBuffer bufferCopy;
        {
                const juce::ScopedLock sl(midiCriticalSection);

                if (overdubHistory.empty())
                        return;

                recordBuffer = overdubHistory.back();
                overdubHistory.pop_back();
                isOverdubbing = false;
                bufferCopy = recordBuffer;
        }

        republishRecordedEvents(bufferCopy);
}

void MidiManager::getRecorded()
{
	// Lock the MIDI critical section
	const juce::ScopedLock sl(midiCriticalSection);

	processRecordedMidi();

	recordBuffer.clear();
	recordStartTime = juce::Time::getHighResolutionTicks();
	overdubHistory.clear();
	isOverdubbing = false;
	isStripped = false;

}

void MidiManager::sendTestNote()
{
	// Lock the MIDI critical section
	const juce::ScopedLock sl(midiCriticalSection);

	// Create a MIDI message to send a test note
	juce::MidiMessage testNoteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)127);
	juce::MidiMessage testNoteOff = juce::MidiMessage::noteOff(1, 60);

	// Send the test note on immediately
	handleIncomingMidiMessage(midiInput.get(), testNoteOn);
	DBG("Test Note Sent: " + testNoteOn.getDescription());

	// Schedule testNoteOff to be sent after 1000 ms using a non-blocking call
	juce::Timer::callAfterDelay(1000, [this, testNoteOff]()
		{
			// Lock the MIDI critical section for thread-safety in the callback if needed
			const juce::ScopedLock sl(midiCriticalSection);
			handleIncomingMidiMessage(midiInput.get(), testNoteOff);
			DBG("Test Note Off Sent: " + testNoteOff.getDescription());
		});
}

void MidiManager::processRecordedMidi()
{
	// Lock the MIDI critical section
	const juce::ScopedLock sl(midiCriticalSection);

	// Bail out if nothing was recorded
	if (recordBuffer.getNumEvents() == 0)
		return;

	// This will hold the events we actually want to keep
	juce::MidiMessageSequence recordedMidi;

	// Timebase conversion setup
	juce::int64 ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();
	double       ticksPerQuarterNote = 960.0;
	double       bpm = 120.0;
	double       tickConversionFactor = (ticksPerQuarterNote * bpm) / (ticksPerSecond * 60.0);

	// Define a 5-second gap in high-res ticks
	juce::int64 bigGapThreshold = static_cast<juce::int64>(5.0 * ticksPerSecond);

	// --- First pass: find the start of the *last* note-group -------------
	juce::int64 lastNoteOnTime = 0;
	juce::int64 startTime = 0;
	bool         foundBigGap = false;

	juce::MidiBuffer::Iterator it(recordBuffer);
	juce::MidiMessage           msg;
	int                         samplePosition;

	while (it.getNextEvent(msg, samplePosition))
	{
		if (msg.isNoteOn())
		{
			juce::int64 ts = msg.getTimeStamp();
			if (lastNoteOnTime != 0 && (ts - lastNoteOnTime) > bigGapThreshold)
			{
				startTime = ts;
				foundBigGap = true;
			}
			lastNoteOnTime = ts;
		}
	}

	// If we never saw a big gap between Note-Ons, start from the very beginning
	if (!foundBigGap)
		startTime = 0;

	// --- Second pass: copy everything from startTime onward -------------
	juce::MidiBuffer::Iterator it2(recordBuffer);
	while (it2.getNextEvent(msg, samplePosition))
	{
		juce::int64 ts = msg.getTimeStamp();
		if (ts >= startTime)
		{
			double timeDiff = (ts - startTime) * tickConversionFactor;
			if (timeDiff < 0.0)
				timeDiff = 0.0;

			DBG("Metadata Time: " + juce::String(ts));
			msg.setTimeStamp(timeDiff);
			recordedMidi.addEvent(msg);
		}
	}

	// --- Debug dump of what we’ll save ------------------
	for (int i = 0; i < recordedMidi.getNumEvents(); ++i)
	{
		auto& e = recordedMidi.getEventPointer(i)->message;
		DBG("Recorded MIDI Event: " + e.getDescription()
			+ " at time " + juce::String(e.getTimeStamp()));
	}

	// Finally, write out to your MIDI file
        saveToMidiFile(recordedMidi);
}



void MidiManager::saveToMidiFile(juce::MidiMessageSequence& recordedMIDI)
{
    // Create OscServer subfolder in user's documents directory
    juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OscServer");
    if (!dawServerDir.exists())
        dawServerDir.createDirectory();

    // Save MIDI file in OscServer subfolder
    juce::File midiFile = dawServerDir.getChildFile("recordedMIDI.mid");

    if (recordedMIDI.getNumEvents() == 0)
    {
        DBG("No MIDI events to save.");
        return;
    }

    juce::String trackName;
    if (mainComponent != nullptr)
    {
        auto channel = mainComponent->getOrchestraTableModel().getSelectedMidiChannel();
        auto pluginId = mainComponent->getOrchestraTableModel().getSelectedPluginId();
        auto channelTags = buildChannelTagMap(pluginId);
        auto it = channelTags.find(channel);
        if (it != channelTags.end())
            trackName = it->second;
    }

    if (trackName.isNotEmpty() && extractTrackName(recordedMIDI).isEmpty())
    {
        auto trackNameMessage = juce::MidiMessage::textMetaEvent(0x03, trackName);
        trackNameMessage.setTimeStamp(0.0);
        recordedMIDI.addEvent(trackNameMessage);
    }

    recordedMIDI.updateMatchedPairs();

    // --- Ensure we replace the file, not append ---
    if (midiFile.existsAsFile())
        midiFile.deleteFile();

    juce::FileOutputStream outputStream(midiFile);
    if (outputStream.openedOk())
    {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(960);
        midi.addTrack(recordedMIDI);
        midi.writeTo(outputStream);
        outputStream.flush();
        DBG("MIDI File Saved: " + midiFile.getFullPathName());
    }
    else
    {
        DBG("Failed to open file for writing MIDI data: " + midiFile.getFullPathName());
    }
}

void MidiManager::exportRecordBufferToMidiFile()
{
        juce::MidiBuffer bufferCopy;
        {
                const juce::ScopedLock sl(midiCriticalSection);
                if (recordBuffer.getNumEvents() == 0)
                {
                        DBG("No MIDI events to export.");
                        return;
                }

                bufferCopy = recordBuffer;
        }

        juce::FileChooser fileChooser("Export MIDI File", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mid");
        if (!fileChooser.browseForFileToSave(true))
                return;

        auto targetFile = fileChooser.getResult();

        auto channelSequences = buildChannelSequences(bufferCopy);

        juce::String pluginId;
        if (mainComponent != nullptr)
                pluginId = mainComponent->getOrchestraTableModel().getSelectedPluginId();

        auto channelTags = buildChannelTagMap(pluginId);

        if (pluginId.isNotEmpty())
        {
                for (auto it = channelSequences.begin(); it != channelSequences.end();)
                {
                        if (channelTags.find(it->first) == channelTags.end())
                                it = channelSequences.erase(it);
                        else
                                ++it;
                }
        }

        for (auto& entry : channelSequences)
        {
                auto it = channelTags.find(entry.first);
                if (it != channelTags.end() && it->second.isNotEmpty())
                        entry.second.trackName = it->second;
                else if (pluginId.isNotEmpty())
                        entry.second.trackName = pluginId + " Ch " + juce::String(entry.first);
                else
                        entry.second.trackName = "Channel " + juce::String(entry.first);
        }

        writeMidiFile(targetFile, channelSequences);
}

void MidiManager::importMidiFileToRecordBuffer()
{
        juce::FileChooser fileChooser("Import MIDI File", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mid");
        if (!fileChooser.browseForFileToOpen())
                return;

        auto midiFileToImport = fileChooser.getResult();
        juce::FileInputStream inputStream(midiFileToImport);

        if (!inputStream.openedOk())
        {
                DBG("Failed to open MIDI file: " + midiFileToImport.getFullPathName());
                return;
        }

        juce::MidiFile midiFile;
        if (!midiFile.readFrom(inputStream))
        {
                DBG("Failed to read MIDI data from file: " + midiFileToImport.getFullPathName());
                return;
        }

        midiFile.convertTimestampTicksToSeconds();

        juce::MidiBuffer newBuffer;
        auto ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();

        juce::String pluginId;
        if (mainComponent != nullptr)
                pluginId = mainComponent->getOrchestraTableModel().getSelectedPluginId();

        auto channelTags = buildChannelTagMap(pluginId);
        std::map<juce::String, int> tagToChannel;
        auto rebuildTagToChannel = [&]()
        {
                tagToChannel.clear();
                for (const auto& entry : channelTags)
                {
                        auto name = entry.second.trim();
                        if (name.isNotEmpty())
                                tagToChannel[name] = entry.first;
                }
        };
        rebuildTagToChannel();

        for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex)
        {
                auto* track = midiFile.getTrack(trackIndex);
                if (track == nullptr)
                        continue;

                auto trackName = extractTrackName(*track).trim();
                int channelForTrack = 0;
                auto channelIt = tagToChannel.find(trackName);
                if (channelIt != tagToChannel.end())
                        channelForTrack = channelIt->second;

                if (channelForTrack == 0)
                        channelForTrack = extractChannelFromTrack(*track);

                if (pluginId.isNotEmpty() && channelForTrack == 0)
                        continue;

                if (pluginId.isNotEmpty() && channelForTrack > 0)
                {
                        if (ensurePluginChannelEntry(pluginId, channelForTrack, trackName))
                        {
                                channelTags = buildChannelTagMap(pluginId);
                                rebuildTagToChannel();
                                auto refreshed = tagToChannel.find(trackName);
                                if (refreshed != tagToChannel.end())
                                        channelForTrack = refreshed->second;
                        }
                }

                if (channelForTrack == 0)
                        channelForTrack = extractChannelFromTrack(*track);

                for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
                {
                        const auto* holder = track->getEventPointer(eventIndex);
                        if (holder == nullptr)
                                continue;

                        const auto& message = holder->message;
                        if (message.isMetaEvent())
                                continue;

                        juce::MidiMessage messageCopy = message;
                        if (channelForTrack > 0)
                                messageCopy.setChannel(channelForTrack);

                        double timeInSeconds = messageCopy.getTimeStamp();
                        juce::int64 timestamp = ticksPerSecond > 0
                                ? static_cast<juce::int64>(timeInSeconds * static_cast<double>(ticksPerSecond))
                                : static_cast<juce::int64>(timeInSeconds * 1000.0);

                        if (timestamp < 0)
                                timestamp = 0;

                        messageCopy.setTimeStamp(static_cast<double>(timestamp));
                        newBuffer.addEvent(messageCopy, static_cast<int>(timestamp));
                }
        }

        if (newBuffer.getNumEvents() == 0)
        {
                DBG("No MIDI events were imported from file: " + midiFileToImport.getFullPathName());
                return;
        }

        {
                const juce::ScopedLock sl(midiCriticalSection);
                recordBuffer = newBuffer;
                overdubHistory.clear();
                isOverdubbing = false;
                recordStartTime = juce::Time::getHighResolutionTicks();
        }

        republishRecordedEvents(newBuffer);
}

int MidiManager::extractChannelFromTrack(const juce::MidiMessageSequence& track)
{
        for (int eventIndex = 0; eventIndex < track.getNumEvents(); ++eventIndex)
        {
                const auto* holder = track.getEventPointer(eventIndex);
                if (holder == nullptr)
                        continue;

                const auto& message = holder->message;
                if (message.isMetaEvent())
                        continue;

                auto channel = message.getChannel();
                if (channel > 0)
                        return juce::jlimit(1, 16, channel);
        }

        return 0;
}

bool MidiManager::ensurePluginChannelEntry(const juce::String& pluginId, int channel, const juce::String& trackName)
{
        if (mainComponent == nullptr || pluginId.isEmpty() || channel <= 0)
                return false;

        auto trimmedName = trackName.trim();
        auto& conductor = mainComponent->getConductor();
        auto& orchestra = conductor.orchestra;

        auto existingIt = std::find_if(orchestra.begin(), orchestra.end(), [&](const InstrumentInfo& instrument)
        {
                return instrument.pluginInstanceId == pluginId && instrument.midiChannel == channel;
        });

        if (existingIt != orchestra.end())
        {
                if (trimmedName.isEmpty())
                        return false;

                auto& instrument = *existingIt;
                auto& tags = instrument.tags;
                bool matchesExisting = tags.size() == 1 && tags.front().compareIgnoreCase(trimmedName) == 0;

                if (!matchesExisting)
                {
                        tags.clear();
                        tags.push_back(trimmedName);
                        instrument.instrumentName = trimmedName;
                        mainComponent->getOrchestraTableModel().table.updateContent();
                        return true;
                }

                return false;
        }

        InstrumentInfo newInstrument;

        auto templateIt = std::find_if(orchestra.begin(), orchestra.end(), [&](const InstrumentInfo& instrument)
        {
                return instrument.pluginInstanceId == pluginId;
        });

        if (templateIt != orchestra.end())
        {
                newInstrument = *templateIt;
        }
        else if (!orchestra.empty())
        {
                newInstrument = orchestra.front();
        }
        else
        {
                newInstrument.instrumentName = trimmedName.isNotEmpty() ? trimmedName : pluginId + " Ch " + juce::String(channel);
                newInstrument.pluginName = pluginId;
                newInstrument.pluginInstanceId = pluginId;
                newInstrument.tags.clear();
        }

        newInstrument.pluginInstanceId = pluginId;
        newInstrument.midiChannel = channel;

        if (trimmedName.isNotEmpty())
        {
                newInstrument.instrumentName = trimmedName;
                newInstrument.tags.clear();
                newInstrument.tags.push_back(trimmedName);
        }
        else
        {
                auto defaultName = pluginId + " Ch " + juce::String(channel);
                newInstrument.instrumentName = defaultName;
                if (newInstrument.tags.empty())
                        newInstrument.tags.push_back(defaultName);
        }

        orchestra.push_back(newInstrument);
        conductor.syncOrchestraWithPluginManager();
        mainComponent->getOrchestraTableModel().table.updateContent();

        return true;
}

bool MidiManager::canUndoOverdub() const
{
        const juce::ScopedLock sl(midiCriticalSection);
        return !overdubHistory.empty();
}

bool MidiManager::hasRecordedEvents() const
{
        const juce::ScopedLock sl(midiCriticalSection);
        return recordBuffer.getNumEvents() > 0;
}

juce::int64 MidiManager::getTimestampFromEvent(const juce::MidiMessage& message, int samplePosition)
{
        auto timestamp = static_cast<juce::int64>(message.getTimeStamp());
        if (timestamp == 0 && samplePosition != 0)
                timestamp = static_cast<juce::int64>(samplePosition);
        return timestamp;
}

void MidiManager::republishRecordedEvents(const juce::MidiBuffer& bufferCopy)
{
        auto pluginId = mainComponent->getOrchestraTableModel().getSelectedPluginId();
        auto& pluginManager = mainComponent->getPluginManager();

        pluginManager.resetPlayback();

        juce::MidiBuffer::Iterator it(bufferCopy);
        juce::MidiMessage msg;
        int samplePosition;
        auto ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();

        while (it.getNextEvent(msg, samplePosition))
        {
                auto ticks = getTimestampFromEvent(msg, samplePosition);
                if (ticks < 0)
                        ticks = 0;

                juce::int64 timestampMs = 0;
                if (ticksPerSecond > 0)
                        timestampMs = static_cast<juce::int64>((static_cast<double>(ticks) * 1000.0) / static_cast<double>(ticksPerSecond));

                juce::MidiMessage messageCopy = msg;
                pluginManager.addMidiMessage(messageCopy, pluginId, timestampMs);
        }

        pluginManager.printTaggedMidiBuffer();
}

void MidiManager::removeMidiChannelFromOverdub(int midiChannel)
{
    if (midiChannel < 1 || midiChannel > 16)
        return;

    juce::MidiBuffer bufferCopy;
    bool removedEvents = false;

    {
        const juce::ScopedLock sl(midiCriticalSection);

        if (recordBuffer.getNumEvents() == 0)
            return;

        juce::MidiBuffer filteredBuffer;
        juce::MidiBuffer::Iterator it(recordBuffer);
        juce::MidiMessage msg;
        int samplePosition;

        while (it.getNextEvent(msg, samplePosition))
        {
            if (msg.getChannel() == midiChannel)
            {
                removedEvents = true;
                continue;
            }

            filteredBuffer.addEvent(msg, samplePosition);
        }

        if (!removedEvents)
            return;

        recordBuffer = std::move(filteredBuffer);
        overdubHistory.clear();
        bufferCopy = recordBuffer;
    }

    if (mainComponent != nullptr)
    {
        mainComponent->getPluginManager().stopAllNotes();
        republishRecordedEvents(bufferCopy);
    }

    DBG("Removed MIDI Channel " + juce::String(midiChannel) + " from overdub buffer.");
}
