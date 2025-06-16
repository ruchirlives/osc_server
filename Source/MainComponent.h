#pragma once

#include <JuceHeader.h>
#include "MidiManager.h"
#include "PluginManager.h"
#include "Conductor.h"

class GlobalLookAndFeel : public juce::LookAndFeel_V4
{
public:
	GlobalLookAndFeel()
	{
		// Customize colours globally here if desired
		setColour(juce::TextEditor::backgroundColourId, juce::Colours::darkgrey);
		setColour(juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha(0.3f));
		setColour(juce::TextEditor::textColourId, juce::Colours::white);
	}

	void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
	{
		auto outlineColour = textEditor.findColour(juce::TextEditor::outlineColourId);
		g.setColour(outlineColour);
		g.drawRoundedRectangle(textEditor.getLocalBounds().toFloat(), 6.0f, 1.5f);
	}

	void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
		bool isMouseOverButton, bool isButtonDown) override
	{
		auto bounds = button.getLocalBounds().toFloat();

		// Draw drop shadow
		juce::DropShadow shadow(juce::Colours::black.withAlpha(0.3f), 5, { 2, 2 });
		shadow.drawForRectangle(g, bounds.toNearestInt());

		// Rounded button background
		g.setColour(backgroundColour);
		g.fillRoundedRectangle(bounds, 6.0f);
	}

	void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
	{
		auto bg = textEditor.findColour(juce::TextEditor::backgroundColourId);
		g.setColour(bg);
		g.fillRoundedRectangle(textEditor.getLocalBounds().toFloat(), 6.0f);
	}

	// You can also override button drawing, sliders, etc. here
};

class RoundedTableWrapper : public juce::Component
{
public:
	RoundedTableWrapper(juce::TableListBox& tableRef) : table(tableRef)
	{
		addAndMakeVisible(table);
	}

	void resized() override
	{
		table.setBounds(getLocalBounds().reduced(1));
	}

	void paint(juce::Graphics& g) override
	{
		g.setColour(juce::Colours::darkgrey);
		g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
	}

private:
	juce::TableListBox& table;
};


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
	Conductor conductor{ pluginManager, midiManager, this }; // Create an instance of the Conductor class

	OrchestraTableModel orchestraTableModel{ conductor.orchestra, orchestraTable, this }; // Create an instance of the OrchestraTableModel class
	// create a similar instance of the PluginTableModel class here and initialize it with pluginManager.pluginList

	GlobalLookAndFeel globalLNF;  // Not static
	RoundedTableWrapper orchestraTableWrapper{ orchestraTable };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
