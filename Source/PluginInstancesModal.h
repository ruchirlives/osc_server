#pragma once

#include <JuceHeader.h>
#include <vector>
#include "PluginManager.h"

class PluginInstancesModal : public juce::Component,
							 private juce::ListBoxModel
{
public:
	explicit PluginInstancesModal(PluginManager& managerRef);

	void refreshInstances();

private:
	void resized() override;
	int getNumRows() override;
	void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
	void listBoxItemClicked(int row, const juce::MouseEvent& event) override;

	bool isPositiveRow(int row) const;
	void handleContextMenu(int row, const juce::MouseEvent& event);
	void renameInstance(const juce::String& pluginId);
	void purgeInstance(const juce::String& pluginId);

	PluginManager& pluginManager;
	std::vector<PluginManager::PluginInstanceInfo> instances;

	juce::Label titleLabel{ "titleLabel", "Plugin Instances" };
	juce::Label countLabel;
	juce::ListBox instanceList{ "instanceList", this };
	juce::TextButton refreshButton{ "Refresh" };
	juce::TextButton closeButton{ "Close" };
};
