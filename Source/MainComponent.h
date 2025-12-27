#pragma once

#include <JuceHeader.h>
#include <array>
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
		: base(juce::Colours::darkslategrey.darker(0.25f)),
		  panel(base.brighter(0.1f)),
		  accent(juce::Colour::fromRGB(90, 224, 255)),
		  shadowColour(juce::Colours::black.withAlpha(0.35f))
	{
		setColour(juce::ResizableWindow::backgroundColourId, base);
		setColour(juce::TextButton::buttonColourId, panel);
		setColour(juce::TextButton::buttonOnColourId, accent);
		setColour(juce::TextButton::textColourOffId, juce::Colours::white);
		setColour(juce::TextButton::textColourOnId, juce::Colours::white);

		setColour(juce::ComboBox::backgroundColourId, base.brighter(0.1f));
		setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.25f));
		setColour(juce::ComboBox::textColourId, juce::Colours::white);

		setColour(juce::Label::textColourId, juce::Colours::whitesmoke);

		setColour(juce::PopupMenu::backgroundColourId, base);
		setColour(juce::PopupMenu::textColourId, juce::Colours::white);
		setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.35f));
		setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

		setColour(juce::Slider::thumbColourId, accent);
		setColour(juce::Slider::trackColourId, panel.brighter(0.2f));
		setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::white.withAlpha(0.2f));

		setColour(juce::TextEditor::backgroundColourId, base.darker(0.5f));
		setColour(juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha(0.3f));
		setColour(juce::TextEditor::textColourId, juce::Colours::white);

		setColour(juce::ListBox::backgroundColourId, panel.darker(0.08f));
		setColour(juce::ListBox::outlineColourId, juce::Colours::white.withAlpha(0.15f));

		setDefaultSansSerifTypefaceName("Segoe UI");
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

		juce::DropShadow shadow(shadowColour, 4, { 2, 2 });
		shadow.drawForRectangle(g, bounds.toNearestInt());

		auto b = backgroundColour.interpolatedWith(juce::Colours::black, isButtonDown ? 0.25f : 0.0f);
		if (isMouseOverButton)
			b = b.brighter(0.05f);

		g.setColour(b);
		g.fillRoundedRectangle(bounds, 6.0f);
	}

	void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool isButtonDown) override
	{
		juce::Font font(14.0f, juce::Font::bold);
		g.setFont(font);
		g.setColour(button.findColour(juce::TextButton::textColourOffId));
		g.drawFittedText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
	}

	void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
		bool shouldDrawButtonAsDown) override
	{
		auto bounds = button.getLocalBounds().toFloat().reduced(4);
		g.setColour(button.getToggleState() ? accent : panel);
		g.fillRoundedRectangle(bounds, 6.0f);

		g.setColour(juce::Colours::white.withAlpha(0.7f));
		g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

		juce::Font font(12.0f, juce::Font::bold);
		g.setFont(font);
		g.setColour(juce::Colours::white);
		g.drawFittedText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
	}

	void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
	{
		auto bg = textEditor.findColour(juce::TextEditor::backgroundColourId);
		g.setColour(bg);
		g.fillRoundedRectangle(textEditor.getLocalBounds().toFloat(), 6.0f);
	}

	void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
	{
		g.setColour(findColour(juce::PopupMenu::backgroundColourId));
		g.fillRoundedRectangle(0, 0, width, height, 6.0f);
		g.setColour(juce::Colours::white.withAlpha(0.15f));
		g.drawRoundedRectangle(0, 0, width, height, 6.0f, 1.0f);
	}

private:
	const juce::Colour base;
	const juce::Colour panel;
	const juce::Colour accent;
	const juce::Colour shadowColour;
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
		auto bounds = getLocalBounds().toFloat();
		auto colour = findColour(juce::ListBox::backgroundColourId);
		auto gradient = juce::ColourGradient(colour.brighter(0.25f), 0.0f, bounds.getY(),
			colour.darker(0.15f), 0.0f, bounds.getBottom(), false);
		g.setGradientFill(gradient);
		g.fillRoundedRectangle(bounds, 6.0f);

		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 2.0f);
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
	void setBpm(double bpm);

	void scanForPlugins();
	void initPlugins();
	void initPluginsList();
	void initMidiInputs();
	void refreshMidiInputs();
    void initAudioDrivers();
    void updateAudioDeviceList();
	void setSelectedAudioDriver(const juce::String& driverName);
	void setSelectedAudioDevice(const juce::String& deviceName);
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
	void setMidiInput(juce::String inputText);
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

        void removeMidiChannelFromOverdub(int midiChannel);

        // Refresh the orchestra table and layout after data changes
        void refreshOrchestraTableUI();


private:
    juce::File pluginFolder; // Use a juce::File object instead of a pointer

    struct LayoutMetrics
    {
        int margin = 20;
        int buttonWidth = 125;
        int buttonHeight = 20;
        int spacingX = 10;
        int spacingY = 10;
        int labelHeight = 20;
        int numButtonRows = 4;
    };

    LayoutMetrics getLayoutMetrics() const;

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
	juce::TextButton playOverdubButton{ "Play Overdub" };
	juce::TextButton triggerOverdubButton{ "Start On Trigger" };
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

    // Add a member to hold the config file path
    juce::File configFile;

    // Helper methods for config
    void loadConfig();
    void saveConfig();

    // Helpers for unique tag and instance id generation
    int getNextTagNumber() const;
    int getNextInstanceNumber() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
