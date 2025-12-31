#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include "MidiManager.h"
#include "PluginManager.h"
#include "Conductor.h"
#include "GlobalLookAndFeel.h"
#include "AudioUdpStreamer.h"

class MainComponent :   public juce::Component, 
                        public juce::ComboBox::Listener
{
public:
    struct RestoreModalController
    {
        std::function<void(const juce::String&)> updateStatus;
        std::function<void()> close;
    };

    MainComponent();
    ~MainComponent() override;

	std::function<void()> onInitialised;

	void moveSelectedRowsToEnd();
	void updateProjectNameLabel(juce::String projectName);
	juce::String getCurrentProjectName() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

	void updateOverdubUI();

	void handleAudioPortChange();

	// Add a TextEditor for BPM input
	juce::Label bpmLabel { "BPM", "BPM" };
	juce::TextEditor bpmEditor;

	// Add a method to get the current BPM
	double getBpm() const;
	void setBpm(double bpm);

	enum class PluginScanMode
	{
		Replace,
		Add
	};
	void scanForPlugins(PluginScanMode mode = PluginScanMode::Replace);
	void initPlugins();
	void initPluginsList();
	void initMidiInputs();
	void refreshMidiInputs();
    void initAudioDrivers();
    void updateAudioDeviceList();
	void setSelectedAudioDriver(const juce::String& driverName);
	void setSelectedAudioDevice(const juce::String& deviceName);
	void initOrchestraTable();
	void showRoutingModal();
	void saveProject(const std::vector<InstrumentInfo>& selectedInstruments = {}, const juce::File& customFile = {});
	void restoreProject(bool append = false, const juce::File& customFile = {});
	RestoreModalController openRestoreModal();

	struct ProjectSaveFiles
	{
		juce::File dataFile;
		juce::File pluginDescriptionsFile;
		juce::File orchestraFile;
		juce::File routingFile;
		juce::File captureBufferFile;
	};

	ProjectSaveFiles getDefaultProjectFiles() const;
	juce::File getDefaultProjectArchiveFile() const;
	bool saveSharedProjectFiles(const ProjectSaveFiles& files, bool includeRoutingData, const std::vector<InstrumentInfo>& selectedInstruments);
	bool restoreProjectFromFiles(const juce::File& dataFile,
	                             const juce::File& pluginDescFile,
	                             const juce::File& orchestraFile,
	                             const juce::File& routingFile,
	                             bool routingExtracted,
	                             const juce::File& bufferFile,
	                             bool bufferExtracted,
	                             bool append,
	                             const juce::String& projectName = {});

	// Add methods for add and remove instrument buttons
	void addInstrument();
	void pasteClipboard(int newRow);
	void addNewInstrument();
	void UpdateAndSelect(int row);
	void basicInstrument(InstrumentInfo& instrument);
	void removeInstrument();

	void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
	void setMidiInput(juce::String inputText);
	void addDataToTable();
	void openPlugins(juce::TableListBox& table);

	bool getFolder();
	Conductor& getConductor() { return conductor; }

	// Getter for orchestraTableModel
	OrchestraTableModel& getOrchestraTableModel() { return orchestraTableModel; }

	// Getter for pluginManager
	PluginManager& getPluginManager() { return pluginManager; }
        MidiManager& getMidiManager() { return midiManager; }
        juce::TableListBox orchestraTable; // TableListBox to display orchestra information

        void removeMidiChannelFromOverdub(int midiChannel);

        // Refresh the orchestra table and layout after data changes
	void refreshOrchestraTableUI();
	void replacePluginForRow(int row, juce::Component* anchor);
	void applyPluginReplacement(int row, const juce::String& pluginName);

private:
    juce::File pluginFolder; // Use a juce::File object instead of a pointer

    LayoutMetrics getLayoutMetrics() const;

    juce::File getDefaultDawServerDir() const;

    struct ButtonPanelLayout
    {
        juce::Rectangle<float> panel;
        std::array<int, 4> rowY{};
    };

    juce::Rectangle<float> computeTablePanelBounds(const LayoutMetrics& metrics, const juce::Rectangle<int>& tableBounds) const;
    ButtonPanelLayout computeButtonPanelLayout(const LayoutMetrics& metrics, const juce::Rectangle<float>& tablePanelBounds) const;

	// Add ComboBoxes for plugin selection and MIDI input selection
    juce::ComboBox pluginBox;  // ComboBox to display plugins
    juce::ComboBox midiInputList; // ComboBox to display MIDI inputs
    juce::Label audioDriverLabel{ "Audio Driver", "Audio Driver" };
    juce::ComboBox audioDriverList;

    // Add a ComboBox for audio device selection within the selected driver
    juce::ComboBox audioDeviceList;
    juce::Label audioDeviceLabel{ "Audio Device", "Audio Device" };

	juce::TextButton getRecordedButton{ "Get and Reset" }; // Button to trigger 
	juce::TextButton listPluginInstancesButton{ "Plugin Instances" }; // Button to list plugin instances
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
	juce::TextButton ScanButton{ "Scan for plugins" }; // Button to Scan for plugins
	juce::TextButton aboutButton{ "About" }; // Button to show app info
	juce::TextButton routingButton{ "Routing" }; // Button to open routing modal

	// Save and restore buttons
	juce::TextButton saveButton{ "Save" }; // Button to save plugin states
	juce::TextButton restoreButton{ "Restore" }; // Button to restore plugin states

	juce::CriticalSection midiCriticalSection; // Critical section to protect the MIDI buffer
	juce::MidiBuffer midiBuffer; // MIDI buffer to store incoming MIDI messages
	MidiManager midiManager{this, midiCriticalSection, midiBuffer }; // Create an instance of the MidiManager class

    // Label for the Project Name
    juce::Label projectNameLabel{ "Project Name", "Project Name" }; // Label for the project name
    juce::String currentProjectName;

	// Create a label and text editor for audio streaming port
	juce::Label audioStreamingPortLabel{ "Audio Streaming Port", "Audio Streaming Port" }; // Label for the audio streaming port
	juce::TextEditor audioStreamingPortEditor; // Text editor for the audio streaming port

        // Buttons for overdub control
    juce::TextButton startOverdubButton { "Start" };
    juce::TextButton stopOverdubButton { "Stop" };
	juce::TextButton playOverdubButton{ "Preview" };
	juce::TextButton triggerOverdubButton{ "Trig" };
	juce::TextButton bakeOverdubButton{ "Bake" };
    juce::TextButton stripLeadingSilenceButton { "Strip Silence" };
    juce::TextButton undoOverdubButton { "Undo" };
    juce::TextButton playCaptureButton{ "Play Capture" };
    juce::TextButton stopCaptureButton{ "Stop" };
    juce::TextButton importMidiButton { "Import dub" };
	juce::TextButton exportMidiButton { "Export dub" };

    juce::TooltipWindow tooltipWindow;

	PluginManager pluginManager { this, midiCriticalSection, midiBuffer }; // Create an instance of the PluginManager class
	Conductor conductor{ pluginManager, midiManager, this }; // Create an instance of the Conductor class

	OrchestraTableModel orchestraTableModel{ conductor.orchestra, orchestraTable, this }; // Create an instance of the OrchestraTableModel class
	// create a similar instance of the PluginTableModel class here and initialize it with pluginManager.pluginList

	GlobalLookAndFeel globalLNF;  // Not static
	RoundedTableWrapper orchestraTableWrapper{ orchestraTable };
	std::unique_ptr<AudioUdpStreamer> audioStreamer;

    // Add a member to hold the config file path
    juce::File configFile;

    // Helper methods for config
	void loadConfig();
	void saveConfig();
	void showPluginScanModal();
	void showPluginInstancesModal();
	void showAboutDialog();
	void updatePluginInstanceReferences(const juce::String& oldId, const juce::String& newId);

    // Helpers for unique tag and instance id generation
    int getNextTagNumber() const;
    int getNextInstanceNumber() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
