#pragma once

#include <JuceHeader.h>
#include "PluginManager.h"
#include "RenamePluginDialog.h"

// Define a new struct to hold instrument information
struct InstrumentInfo
{
    juce::String instrumentName;
	juce::String pluginName;
    juce::String pluginInstanceId;
    int midiChannel{ 0 };
    std::vector<juce::String> tags;

};


// Conductor class
class Conductor : public juce::OSCReceiver,
    public juce::OSCReceiver::ListenerWithOSCAddress<juce::OSCReceiver::MessageLoopCallback>,
    public juce::OSCSender
{
public:
    // Constructor: takes a reference to PluginManager
    Conductor(PluginManager& pm, MidiManager& mm, MainComponent* mainComponentRef);

    // Destructor
    ~Conductor() override;

    // Initialize OSC Sender with a specific host and port
    void initializeOSCSender(const juce::String& host, int port);
    void sendOSCMessage(const std::vector<juce::String>& tags);

    void send_lastTag();

    // Initialize OSC Receiver with a specific port
    void initializeOSCReceiver(int port);

    // Convert string array to vector
    void stringArrayToVector(juce::StringArray stringArray, std::vector<juce::String>& stringVector);

    // Callback function for receiving OSC messages
    void oscAddInstrumentCommand(const juce::OSCMessage& message);
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void oscProcessMIDIMessage(const juce::OSCMessage& message);

    juce::int64 getTimestamp(const juce::OSCArgument timestampArg);
	juce::int64 adjustTimestamp(const juce::OSCArgument timestamp);

    // Save and restore orchestra state
    void saveOrchestraData(const juce::String& filePath, const std::vector<InstrumentInfo>& selectedInstruments);
    void restoreOrchestraData(const juce::String& filePath);

    void importOrchestraData(const juce::String& dataFilePath);

    void upsertAllData(const juce::String& dataFilePath, const juce::String& pluginDescFilePath, const juce::String& orchestraFilePath);
    void saveAllData(const juce::String& dataFilePath, const juce::String& pluginDescFilePath, const juce::String& orchestraFilePath, const std::vector<InstrumentInfo>& selectedInstruments = {});
    void restoreAllData(const juce::String& dataFilePath, const juce::String& pluginDescFilePath, const juce::String& orchestraFilePath);

    // Vector to hold instrument information
    std::vector<InstrumentInfo> orchestra;

    // Synchronize the orchestra with the PluginManager
    void syncOrchestraWithPluginManager();

	// juce::int64 variable to store the timestamp offset
	juce::int64 timestampOffset = 0;

private:
    // Reference to the PluginManager
    PluginManager& pluginManager;
    MidiManager& midiManager; // Add a reference to the MidiManager


    // Helper function to extract tags from the OSC message
    std::vector<juce::String> extractTags(const juce::OSCMessage& message, int startIndex);
    int calculateSampleOffsetForMessage(const juce::Time& messageTime, double sampleRate);
    std::vector<std::pair<juce::String, int>> extractPluginIdsAndChannels(const juce::OSCMessage& message, int startIndex);

    std::vector<juce::String> lastTags = {};
    // Handles incoming OSC messages
    void handleIncomingNote(juce::String messageType, int channel, int note, int velocity, const juce::String& pluginId, juce::int64& timestamp);
    void handleIncomingProgramChange(int channel, int programNumber, const juce::String& pluginId, juce::int64& timestamp);
    void handleIncomingControlChange(int channel, int controllerNumber, int controllerValue, const juce::String& pluginId, juce::int64& timestamp);
    MainComponent* mainComponent;  // Reference to the MainComponent object


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Conductor)
};

class OrchestraTableModel : public juce::TableListBoxModel
{
public:
    OrchestraTableModel(std::vector<InstrumentInfo>& data, juce::TableListBox& tableRef, MainComponent* mainComponentRef);

    int getNumRows() override;

    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;

    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    static juce::String convertVectorToString(const std::vector<juce::String>& vector);

    // Helper functions to manage the data retrieval and update
    juce::String getText(int columnNumber, int rowNumber) const;

    void setText(int columnNumber, int rowNumber, const juce::String& newText);

    void selectRow(int row, const juce::ModifierKeys& modifiers);
    void renamePluginInstance(int rowNumber);

    void sendTags(int row);
	int getSelectedMidiChannel();
	juce::String getSelectedPluginId();

    // Refresh component for editable cells
    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
        juce::Component* existingComponentToUpdate) override;

    juce::TableListBox& table;  // Reference to the table to select rows
	std::vector<InstrumentInfo>& orchestraData;  // Reference to the orchestra data
	// Conductor& conductor;  // Reference to the Conductor object
	MainComponent* mainComponent;  // Reference to the MainComponent object

private:
};

// Custom component to edit text in the table
class EditableTextCustomComponent : public juce::Label
{
public:
    EditableTextCustomComponent(OrchestraTableModel& ownerRef);
    void mouseDown(const juce::MouseEvent& event) override;
	void showContextMenu_name();
	void save_selection();
    void showContextMenu_tags();
    void getTagsPresetList(std::function<void(const juce::String&, int)> callback);    void showContextMenu_pluginInstances();
    void renameReferencesForSelectedRows();
    void iterate_pluginInstances();
    void showContextMenu_midiChannels();
    void actionContextSelection(const juce::String& text, int columnId);
    void textWasEdited() override;
    void setRowAndColumn(int newRow, int newColumn);

private:
    OrchestraTableModel& owner;
    int row, columnId;
};

