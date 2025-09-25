/*
  ==============================================================================

    MidiManager.h
    Created: 16 Sep 2024 4:18:39pm
    Author:  Desktop

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>

class MainComponent; // Forward declaration

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
	void stripLeadingSilence();
	void undoLastOverdub();
	void getRecorded();
        void startRecording();
        void sendTestNote();

        void exportRecordBufferToMidiFile();
        void importMidiFileToRecordBuffer();

        bool canUndoOverdub() const;
        bool hasRecordedEvents() const;

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



private:
        struct ChannelTrackInfo
        {
                juce::MidiMessageSequence sequence;
                juce::String trackName;
        };

        // MIDI Input
        std::unique_ptr<juce::MidiInput> midiInput; // MIDI Input object
        juce::MidiBuffer recordBuffer; // MIDI Buffer to store recorded MIDI messages
        juce::int64 recordStartTime; // Start time for recording MIDI messages

	juce::CriticalSection& midiCriticalSection; // Critical section to protect the MIDI buffer
	juce::MidiBuffer& incomingMidi; // MIDI Buffer to store incoming MIDI messages

	MainComponent* mainComponent; // Pointer to the MainComponent

        std::vector<juce::MidiBuffer> overdubHistory;

        static juce::int64 getTimestampFromEvent(const juce::MidiMessage& message, int samplePosition);
        void republishRecordedEvents(const juce::MidiBuffer& bufferCopy);

        std::map<int, juce::String> buildChannelTagMap(const juce::String& pluginId) const;
        static juce::String serialiseTags(const std::vector<juce::String>& tags);
        static juce::String extractTrackName(const juce::MidiMessageSequence& sequence);
        std::map<int, ChannelTrackInfo> buildChannelSequences(const juce::MidiBuffer& bufferCopy) const;
        void writeMidiFile(const juce::File& file, const std::map<int, ChannelTrackInfo>& channelSequences) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiManager)
};
