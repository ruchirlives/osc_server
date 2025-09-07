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
#include <map>

void MidiManager::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
	// DBG("MIDI Message Received: " + message.getDescription() + " from " + source->getName());
	// Forward the MIDI message to the buffee is their is a plugin instance

	const juce::ScopedLock sl(midiCriticalSection); // Use the custom CriticalSection for thread safety

	// Always record MIDI events while the plugin is active
	if (midiInput != nullptr)
	{
		//const juce::ScopedLock sl(midiCriticalSection); // Ensure thread safety while accessing recordBuffer

                // Use high-resolution ticks for accurate MIDI event timing
                juce::int64 currentTimeTicks = juce::Time::getHighResolutionTicks();
                DBG("Current Time Ticks: " + juce::String(currentTimeTicks - recordStartTime));

		// Get the current midi channel from selected row in the table from MainComponent
		int midiChannel = mainComponent->getOrchestraTableModel().getSelectedMidiChannel(); // Get the selected MIDI channel from the table

		DBG("MIDI Channel: " + juce::String(midiChannel));

		// Create a new MIDI message with the selected MIDI channel
		juce::MidiMessage messageWithChannel = message;
		messageWithChannel.setChannel(midiChannel);
		incomingMidi.addEvent(messageWithChannel, 0);

                // Stamp the MIDI message with high-resolution ticks directly
                recordBuffer.addEvent(messageWithChannel, currentTimeTicks);
        }
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


void MidiManager::newRecording()
{
        // Lock the MIDI critical section
        const juce::ScopedLock sl(midiCriticalSection);

        // Clear existing recording data
        recordBuffer.clear();
        trackSequence.clear();
        overdubPasses.clear();

        // Reset playback so the buffer starts from the beginning
        mainComponent->getPluginManager().resetPlayback();

        // Zero the recording offset
        recordStartTime = juce::Time::getHighResolutionTicks();
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

void MidiManager::overdubPass()
{
        const juce::ScopedLock sl(midiCriticalSection);

        if (recordBuffer.getNumEvents() == 0)
                return;

        juce::MidiBuffer::Iterator it(recordBuffer);
        juce::MidiMessage msg;
        int samplePosition;
        juce::MidiMessageSequence passSequence;

        juce::int64 passStart = recordStartTime;

        juce::int64 ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();
        double ticksPerQuarterNote = 960.0;
        double bpm = mainComponent->getBpm();
        double tickConversionFactor = (ticksPerQuarterNote * bpm) / (ticksPerSecond * 60.0);

        while (it.getNextEvent(msg, samplePosition))
        {
                juce::int64 ts = msg.getTimeStamp();
                if (ts >= recordStartTime)
                {
                        double timeDiff = (ts - recordStartTime) * tickConversionFactor;
                        msg.setTimeStamp(timeDiff);
                        passSequence.addEvent(msg);
                }
        }

        trackSequence.addSequence(passSequence, 0.0);
        trackSequence.updateMatchedPairs();

        overdubPasses.push_back({ passSequence, passStart });

        recordStartTime = juce::Time::getHighResolutionTicks();
}

void MidiManager::undoLastOverdub()
{
        const juce::ScopedLock sl(midiCriticalSection);

        if (overdubPasses.empty())
                return;

        auto lastPass = overdubPasses.back();
        overdubPasses.pop_back();

        trackSequence.clear();
        for (const auto& pass : overdubPasses)
                trackSequence.addSequence(pass.sequence, 0.0);
        trackSequence.updateMatchedPairs();

        juce::MidiBuffer newBuffer;
        juce::MidiBuffer::Iterator it(recordBuffer);
        juce::MidiMessage msg;
        int samplePosition;
        while (it.getNextEvent(msg, samplePosition))
        {
                if ((juce::int64)samplePosition < lastPass.startTime)
                        newBuffer.addEvent(msg, samplePosition);
        }
        recordBuffer.swapWith(newBuffer);

        recordStartTime = lastPass.startTime;
}

void MidiManager::replay()
{
        undoLastOverdub();
        const juce::ScopedLock sl(midiCriticalSection);

        // Restart playback from the beginning of the current buffer
        mainComponent->getPluginManager().resetPlayback();

        // Reset timing for the next overdub pass
        recordStartTime = juce::Time::getHighResolutionTicks();
}

void MidiManager::saveRecording()
{
        const juce::ScopedLock sl(midiCriticalSection);

        if (trackSequence.getNumEvents() == 0)
                return;

        saveToMidiFile(trackSequence);
        trackSequence.clear();
        recordBuffer.clear();
        recordStartTime = juce::Time::getHighResolutionTicks();
}

void MidiManager::saveToMidiFile(juce::MidiMessageSequence& recordedMIDI)
{
        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(960);

        // Map MIDI channels to their corresponding tag strings
        std::map<int, juce::String> channelTags;
        for (const auto& instrument : mainComponent->getConductor().orchestra)
        {
                juce::String tagsString = OrchestraTableModel::convertVectorToString(instrument.tags);
                channelTags[instrument.midiChannel] = tagsString;
        }

        // Group events into tracks based on tag strings, ignoring channels
        std::map<juce::String, juce::MidiMessageSequence> tagSequences;
        for (int i = 0; i < recordedMIDI.getNumEvents(); ++i)
        {
                const auto& msg = recordedMIDI.getEventPointer(i)->message;
                juce::String tag = channelTags.count(msg.getChannel())
                        ? channelTags[msg.getChannel()]
                        : juce::String("Channel ") + juce::String(msg.getChannel());
                tagSequences[tag].addEvent(msg);
        }

        for (auto& entry : tagSequences)
        {
                juce::String name = entry.first;
                auto& seq = entry.second;

                seq.addEvent(juce::MidiMessage::createTrackNameEvent(name), 0.0);
                seq.addEvent(juce::MidiMessage::createInstrumentNameEvent(name), 0.0);
                seq.sort();
                seq.updateMatchedPairs();
                midiFile.addTrack(seq);
        }

        juce::File midiFileToSave = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("recordedMIDI.mid");

        if (midiFileToSave.existsAsFile())
        {
                midiFileToSave.deleteFile();
        }

        juce::FileOutputStream stream(midiFileToSave);
        midiFile.writeTo(stream);
        stream.flush();

        DBG("MIDI File Saved: " + midiFileToSave.getFullPathName());
}

