#pragma once

#include <JuceHeader.h>
#include "MidiManager.h"
#include "PluginManager.h"
#include "Conductor.h"

#pragma once
#include <JuceHeader.h>

class AudioUdpStreamer
{
public:
	AudioUdpStreamer(const juce::String& ip, int port)
		: targetIp(ip), targetPort(port)
	{
		socket = std::make_unique<juce::DatagramSocket>();
	}

	void sendAudio(const juce::AudioBuffer<float>& buffer)
	{
		const int numSamples = buffer.getNumSamples();
		const int numChannels = buffer.getNumChannels();
		tempBuffer.setSize(numChannels, numSamples, false, false, true);
		tempBuffer.makeCopyOf(buffer);

		std::vector<uint8_t> bytes;
		bytes.reserve(numSamples * numChannels * 2); // 2 bytes per sample

		for (int i = 0; i < numSamples; ++i)
		{
			for (int ch = 0; ch < numChannels; ++ch)
			{
				float sample = tempBuffer.getSample(ch, i);
				int16_t s16 = static_cast<int16_t>(juce::jlimit(-1.0f, 1.0f, sample) * 32767);
				bytes.push_back(static_cast<uint8_t>(s16 & 0xFF));
				bytes.push_back(static_cast<uint8_t>((s16 >> 8) & 0xFF));
			}
		}

		socket->write(targetIp, targetPort, bytes.data(), static_cast<int>(bytes.size()));
	}

	void setPort(int port)
	{
		targetPort = port;
		if (socket)
		{
			socket->bindToPort(targetPort);
		}
		else
		{
			socket = std::make_unique<juce::DatagramSocket>();
			socket->bindToPort(targetPort);
		}
	}

private:
	juce::String targetIp;
	int targetPort;
	std::unique_ptr<juce::DatagramSocket> socket;
	juce::AudioBuffer<float> tempBuffer;
};


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

	void updateOverdubUI();

	void handleAudioPortChange();

	// Add a TextEditor for BPM input
	juce::TextEditor bpmEditor;

	// Add a method to get the current BPM
	double getBpm() const;

	void scanForPlugins();
	void initPlugins();
        void initMidiInputs();
        void initAudioDrivers();
	void initOrchestraTable();
	void saveProject(const std::vector<InstrumentInfo>& selectedInstruments = {});
	void restoreProject(bool append = false);

	// Add methods for add and remove instrument buttons
	void addInstrument();
	void pasteClipboard(int newRow);
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
	MidiManager& getMidiManager() { return midiManager; }
	juce::TableListBox orchestraTable; // TableListBox to display orchestra information


private:
    juce::File pluginFolder; // Use a juce::File object instead of a pointer

	// Add ComboBoxes for plugin selection and MIDI input selection
        juce::ComboBox pluginBox;  // ComboBox to display plugins
        juce::ComboBox midiInputList; // ComboBox to display MIDI inputs
        juce::Label audioDriverLabel{ "Audio Driver", "Audio Driver" };
        juce::ComboBox audioDriverList;

	juce::TextButton getRecordedButton{ "Get and Reset" }; // Button to trigger 
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
	MidiManager midiManager{this, midiCriticalSection, midiBuffer }; // Create an instance of the MidiManager class

	// Label for the Project Name
	juce::Label projectNameLabel{ "Project Name", "Project Name" }; // Label for the project name

	// Create a label and text editor for audio streaming port
	juce::Label audioStreamingPortLabel{ "Audio Streaming Port", "Audio Streaming Port" }; // Label for the audio streaming port
	juce::TextEditor audioStreamingPortEditor; // Text editor for the audio streaming port

        // Buttons for overdub control
    juce::TextButton startOverdubButton { "Start Overdub" };
    juce::TextButton stopOverdubButton { "Stop Overdub" };
    juce::TextButton stripLeadingSilenceButton { "Strip Silence" };
    juce::TextButton undoOverdubButton { "Undo Overdub" };
        juce::TextButton importMidiButton { "Import MIDI" };
        juce::TextButton exportMidiButton { "Export MIDI" };

	PluginManager pluginManager { this, midiCriticalSection, midiBuffer }; // Create an instance of the PluginManager class
	Conductor conductor{ pluginManager, midiManager, this }; // Create an instance of the Conductor class

	OrchestraTableModel orchestraTableModel{ conductor.orchestra, orchestraTable, this }; // Create an instance of the OrchestraTableModel class
	// create a similar instance of the PluginTableModel class here and initialize it with pluginManager.pluginList

	GlobalLookAndFeel globalLNF;  // Not static
	RoundedTableWrapper orchestraTableWrapper{ orchestraTable };
	std::unique_ptr<AudioUdpStreamer> audioStreamer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
