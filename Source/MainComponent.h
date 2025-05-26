#pragma once

#include <JuceHeader.h>
#include "MidiManager.h"
#include "PluginManager.h"
#include "Conductor.h"

class MainComponent :   public juce::Component, 
                        public juce::ComboBox::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

	void moveSelectedRowsToEnd();

	void updateProjectNameLabel(juce::String projectName);

    void paint(juce::Graphics& g) override;
    void resized() override;

	// Add a TextEditor for BPM input
	juce::TextEditor bpmEditor;

	// Add a method to get the current BPM
	double getBpm() const;

	void scanForPlugins();
	void initPlugins();
	void initMidiInputs();
	void initOrchestraTable();
	void saveProject(const std::vector<InstrumentInfo>& selectedInstruments = {});
	void restoreProject(bool append = false);

	// Add methods for add and remove instrument buttons
	void addInstrument();
	void addNewInstrument();
	void UpdateAndSelect(int row);
	void basicInstrument(InstrumentInfo& instrument);
	void removeInstrument();

	void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
	void addDataToTable();
	void openPlugins(juce::TableListBox& table);

	void getFolder();
	Conductor& getConductor() { return conductor; }

	// Getter for orchestraTableModel
	OrchestraTableModel& getOrchestraTableModel() { return orchestraTableModel; }

	// Getter for pluginManager
	PluginManager& getPluginManager() { return pluginManager; }
	MidiManager midiManager{this, midiCriticalSection, midiBuffer }; // Create an instance of the MidiManager class
	juce::TableListBox orchestraTable; // TableListBox to display orchestra information


private:
    juce::File pluginFolder; // Use a juce::File object instead of a pointer

	// Add ComboBoxes for plugin selection and MIDI input selection
    juce::ComboBox pluginBox;  // ComboBox to display plugins
	juce::ComboBox midiInputList; // ComboBox to display MIDI inputs

	juce::TextButton getRecordedButton{ "Get Recorded" }; // Button to trigger getRecorded
	juce::TextButton startRecordingButton{ "Start Recording" }; // Button to trigger startRecording

	juce::TextButton updateButton{ "Update" }; // Button to refresh the orchestra table
	juce::TextButton listPluginInstancesButton{ "List Plugin Instances" }; // Button to list plugin instances
	juce::TextButton sendTestNoteButton{ "Send Test Note" }; // Button to send a test note

	// Buttons to add and remove instruments from the orchestra
	juce::TextButton addInstrumentButton{ "Add Instrument" }; // Button to add an instrument to the orchestra
	juce::TextButton addNewInstrumentButton{ "Add New Instrument" }; // Button to add a new instrument to the orchestra
	juce::TextButton removeInstrumentButton{ "Remove Instrument" }; // Button to remove an instrument from the orchestra

	// Button to move selected rows to end of orchestra
	juce::TextButton moveToEndButton{ "Move to End" }; // Button to move selected rows to the end of the orchestra

	// Add a TextButton to open the plugin window for each selected row in the orchestra table
	juce::TextButton openPluginButton{ "Open Plugin" }; // Button to open the plugin window

	// Scan for plugins button
	juce::TextButton ScanButton{ "Scan" }; // Button to Scan for plugins

	// Save and restore buttons
	juce::TextButton saveButton{ "Save" }; // Button to save plugin states
	juce::TextButton restoreButton{ "Restore" }; // Button to restore plugin states

	juce::CriticalSection midiCriticalSection; // Critical section to protect the MIDI buffer
	juce::MidiBuffer midiBuffer; // MIDI buffer to store incoming MIDI messages

	// Label for the Project Name
	juce::Label projectNameLabel{ "Project Name", "Project Name" }; // Label for the project name

	PluginManager pluginManager { this, midiCriticalSection, midiBuffer }; // Create an instance of the PluginManager class
	Conductor conductor{ pluginManager, midiManager }; // Create an instance of the Conductor class

	OrchestraTableModel orchestraTableModel{ conductor.orchestra, orchestraTable, this }; // Create an instance of the OrchestraTableModel class
	// create a similar instance of the PluginTableModel class here and initialize it with pluginManager.pluginList

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
