#pragma once

#include <JuceHeader.h>

class PluginManager;

class PluginScanModal : public juce::Component,
						private juce::ListBoxModel
{
public:
	PluginScanModal(PluginManager& pluginManagerRef,
		std::function<void()> scanReplaceAction,
		std::function<void()> scanAddAction);

	void refreshPluginList();

	int getNumRows() override;
	void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

private:
	void resized() override;

	PluginManager& pluginManager;
	std::function<void()> scanReplaceCallback;
	std::function<void()> scanAddCallback;

	juce::Label titleLabel{ "titleLabel", "Available Plugins" };
	juce::ListBox pluginListBox{ "pluginListBox", this };
	juce::TextButton scanReplaceButton{ "Scan Replace" };
	juce::TextButton scanAddButton{ "Scan Add" };
	juce::TextButton closeButton{ "Close" };
	juce::Label countLabel;

	juce::StringArray pluginNames;
};
