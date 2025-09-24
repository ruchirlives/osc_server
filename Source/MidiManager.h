/*
  ==============================================================================

    MidiManager.h
    Created: 16 Sep 2024 4:18:39pm
    Author:  Desktop

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class MainComponent; // Forward declaration
class MidiManager;


class PlaybackThread : public juce::Thread
{
public:
    PlaybackThread(MidiManager& manager)
        : juce::Thread("MidiPlaybackThread"), midiManager(manager) {}

    void run() override;
	std::unique_ptr<PlaybackThread> playbackThread;

private:
    MidiManager& midiManager;
};

class MidiManager : public juce::MidiInputCallback
{
public:
	MidiManager(MainComponent * mainComponent, juce::CriticalSection& criticalSection, juce::MidiBuffer& midiBuffer) : mainComponent(mainComponent), midiCriticalSection(criticalSection), incomingMidi(midiBuffer), recordStartTime(juce::Time::getHighResolutionTicks()) {};
	~MidiManager() { closeMidiInput(); };

	// MIDI Input Callback
	void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
	void openMidiInput(juce::String midiInputName);
	
	void closeMidiInput();
	void startOverdub();
	void stopOverdub();
	void getRecorded();
	void startRecording();
	void sendTestNote();

	// Declaration of the function to process recorded MIDI
	void processRecordedMidi();
	bool isOverdubbing = false;

	// Access to the MIDI buffer
	juce::MidiBuffer& getMidiBuffer() { return incomingMidi; }

	// Lock access for thread safety
	juce::CriticalSection& getCriticalSection() { return midiCriticalSection; }

	// Save to MIDI file
	void saveToMidiFile(juce::MidiMessageSequence& recordedMIDI);

	std::unique_ptr<juce::Thread> playbackThread;
	std::atomic<bool> playbackThreadShouldRun{ false };

	void startPlaybackThread();
	void stopPlaybackThread();
	void playbackThreadFunc();


private:
	// MIDI Input
	std::unique_ptr<juce::MidiInput> midiInput; // MIDI Input object
	juce::MidiBuffer recordBuffer; // MIDI Buffer to store recorded MIDI messages
	juce::int64 recordStartTime; // Start time for recording MIDI messages

	juce::CriticalSection& midiCriticalSection; // Critical section to protect the MIDI buffer
	juce::MidiBuffer& incomingMidi; // MIDI Buffer to store incoming MIDI messages

	MainComponent* mainComponent; // Pointer to the MainComponent

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiManager)
};