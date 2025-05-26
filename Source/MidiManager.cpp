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
		juce::int64 currentTimeTicks = juce::Time::getHighResolutionTicks()-recordStartTime;
		DBG("Current Time Ticks: " + juce::String(currentTimeTicks));

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



void MidiManager::getRecorded()
{
	// Lock the MIDI critical section
	const juce::ScopedLock sl(midiCriticalSection);

	processRecordedMidi();

	recordBuffer.clear();
	recordStartTime = juce::Time::getHighResolutionTicks();

}

void MidiManager::startRecording()
{
	// Lock the MIDI critical section
	const juce::ScopedLock sl(midiCriticalSection);

	// Clear the record buffer and set the start time
	recordBuffer.clear();
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
	juce::MidiFile midiFile;  // Create a MidiFile object
	midiFile.setTicksPerQuarterNote(960);  // Set the ticks per quarter note (can adjust based on your needs)

	// Add the recorded MIDI events to the MidiFile object
	midiFile.addTrack(recordedMIDI);

	// Create a file to save the MIDI data
	juce::File midiFileToSave = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("recordedMIDI.mid");

	// If you want to ensure the file is fresh, delete it if it exists before proceeding
	if (midiFileToSave.existsAsFile())
	{
		midiFileToSave.deleteFile();  // Delete the existing file
	}

	juce::FileOutputStream stream(midiFileToSave);

	// Save the MIDI data to the file
	midiFile.writeTo(stream);

	stream.flush();

	DBG("MIDI File Saved: " + midiFileToSave.getFullPathName());

}

