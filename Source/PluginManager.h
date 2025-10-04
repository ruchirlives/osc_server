#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

#include "MidiManager.h"
#include "PluginWindow.h"
#include "HostPlayHead.h"


// Forward declaration
class MainComponent;

// Structure to hold MIDI messages and associated tags
struct MyMidiMessage
{
    juce::MidiMessage message;
    juce::String pluginId;
	juce::int64 timestamp;

    // Equality operator
    bool operator==(const MyMidiMessage& other) const
    {
        // Compare timestamps
        if (timestamp != other.timestamp)
            return false;

        // Compare plugin IDs
        if (pluginId != other.pluginId)
            return false;

        // Compare MidiMessage by raw data
        return message.getRawDataSize() == other.message.getRawDataSize() &&
            std::memcmp(message.getRawData(), other.message.getRawData(), message.getRawDataSize()) == 0;
    }
    // Constructor that also takes sampleOffset as an argument
	MyMidiMessage(const juce::MidiMessage& msg, const juce::String& message_pluginId, juce::int64 timestamp) : message(msg), pluginId(message_pluginId), timestamp(timestamp) {}
    
};


class PluginManager : public juce::AudioAppComponent
{
public:
    // In PluginManager.h (or wherever you want to define it)
    struct PlayHeadImpl : public juce::AudioPlayHead
    {
        juce::AudioPlayHead::PositionInfo positionInfo;

        // *** Must match JUCE8 exactly: ***
        juce::Optional<juce::AudioPlayHead::PositionInfo>
            getPosition() const override
        {
            return positionInfo;
        }
    };
    PlayHeadImpl playHead;   // <--- keep one instance
    PluginManager(MainComponent*, juce::CriticalSection&, juce::MidiBuffer&);
    ~PluginManager() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void setBpm(double bpm);
    int playStartCounter = 0;
    static bool playStartIssued;
    static bool midiStartSent;

    juce::PluginDescription getDescFromName(const juce::String& name);

    void instantiatePluginByName(const juce::String& name, const juce::String& pluginId);

    juce::StringArray getPluginInstanceIds() const;

    // Methods to manage plugins
    void instantiatePlugin(juce::PluginDescription* desc, const juce::String& pluginId);
    void openPluginWindow(juce::String pluginId);
	void instantiateSelectedPlugin(juce::PluginDescription* desc);
	juce::String getPluginData(juce::String pluginId);
    void resetPlugin(const juce::String& pluginId);
    void resetAllPlugins();

    // Plugin management
    bool hasPluginInstance(const juce::String& pluginId);
    juce::KnownPluginList knownPluginList;
	void listPluginInstances();
	void savePluginData(const juce::String& dataFilePath, const juce:: String & filename, const juce::String& pluginId);

    juce::String getPluginUniqueId(const juce::String& pluginId);

    // Adds a tagged MIDI message to the taggedMidiBuffer
	void addMidiMessage(const juce::MidiMessage& message, const juce::String& pluginId, juce::int64& timestamp);
	void resetPlayback();

    void stopAllNotes();

    juce::int8 getNumInstances(std::vector<juce::String>& instances);

    void savePluginDescriptionsToFile(const juce::String& dataFilePath, std::vector<juce::String> instances = {});
	void restorePluginDescriptionsFromFile(const juce::String& dataFilePath);
	void upsertPluginDescriptionsFromFile(const juce::String& dataFilePath);

	// Methods to manage plugin state, save and restore
	void scanPlugins(juce::FileSearchPath searchPath);
    bool loadPluginListFromFile();
    void savePluginListToFile();

    void clearTaggedMidiBuffer();
    void printTaggedMidiBuffer();

    juce::MemoryBlock getPluginState(const juce::String& pluginId);
    void restorePluginState(const juce::String& pluginId, const juce::MemoryBlock& state);

    void saveAllPluginStates(const juce::String& dataFilePath, std::vector<juce::String> instances = {});
    void restoreAllPluginStates(const juce::String& dataFilePath);

    // Update getter method to return inherited device manager
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }


    void renamePluginInstance(const juce::String& oldId, const juce::String& newId);
    std::function<void(const juce::AudioBuffer<float>&)> audioTapCallback;


private:
    mutable juce::CriticalSection pluginInstanceLock;
    std::map<juce::String, std::unique_ptr<juce::AudioPluginInstance>> pluginInstances;
    std::map<juce::String, std::unique_ptr<PluginWindow>> pluginWindows;

    // A vector to hold tagged MIDI messages
    std::vector<MyMidiMessage> taggedMidiBuffer;
    std::map<juce::String, std::map<int, std::vector<juce::String>>> channelTagsMap;

    // juce::AudioDeviceManager deviceManager;  // Remove this line
    juce::AudioPluginFormatManager formatManager;

    juce::CriticalSection& midiCriticalSection;
    juce::MidiBuffer& incomingMidi;

    // playback counter
	juce::int64 playbackSamplePosition = 0;
    double currentBpm = 125.0; // Default BPM
    double currentSampleRate = 44100.0;
    juce::int64  totalSamplesProcessed{ 0 };
    MainComponent* mainComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginManager)
};

// Create a new class called PluginTableModel that inherits from TableListBoxModel. It will hold a reference to the PluginManager.pluginList and implement the necessary methods to display the plugin list in the table.
