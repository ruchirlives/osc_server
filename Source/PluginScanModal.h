#pragma once

#include <JuceHeader.h>

class PluginManager;

class PluginScanModal : public juce::Component,
						private juce::ListBoxModel
{
public:
	PluginScanModal(PluginManager& pluginManagerRef,
		std::function<void()> scanReplaceAction,
		std::function<void()> scanAddAction,
		std::function<void()> listChangedAction);
	~PluginScanModal() override;

	void refreshPluginList();

	int getNumRows() override;
	void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
	void selectedRowsChanged(int lastRowSelected) override;

	private:
		struct DragSelectHandler : public juce::MouseListener
		{
			void attachTo(juce::ListBox& listBoxRef);
			void detach();
			int getRowAtEventPosition(const juce::MouseEvent& event) const;
			void mouseDown(const juce::MouseEvent& event) override;
			void mouseDrag(const juce::MouseEvent& event) override;
			void mouseUp(const juce::MouseEvent& event) override;

			juce::ListBox* listBox = nullptr;
		int anchorRow = -1;
		bool isDragging = false;
	};

	void resized() override;
	void removeSelectedPlugins();
	void updateActionButtons();

	PluginManager& pluginManager;
	std::function<void()> scanReplaceCallback;
	std::function<void()> scanAddCallback;
	std::function<void()> listChangedCallback;

	juce::Label titleLabel{ "titleLabel", "Available Plugins" };
	juce::ListBox pluginListBox{ "pluginListBox", this };
	DragSelectHandler dragSelectHandler;
	juce::TextButton scanReplaceButton{ "Scan Replace" };
	juce::TextButton scanAddButton{ "Scan Add" };
	juce::TextButton removeSelectedButton{ "Remove Selected" };
	juce::TextButton closeButton{ "Close" };
	juce::Label countLabel;

	juce::StringArray pluginNames;
};
