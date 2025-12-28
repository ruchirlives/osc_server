#include "PluginScanModal.h"
#include "PluginManager.h"

PluginScanModal::PluginScanModal(PluginManager& pluginManagerRef,
	std::function<void()> scanReplaceAction,
	std::function<void()> scanAddAction)
	: pluginManager(pluginManagerRef),
	scanReplaceCallback(std::move(scanReplaceAction)),
	scanAddCallback(std::move(scanAddAction))
{
	titleLabel.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(titleLabel);

	countLabel.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(countLabel);

	addAndMakeVisible(pluginListBox);
	pluginListBox.setRowHeight(24);
	pluginListBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);

	scanReplaceButton.onClick = [this]()
	{
		if (scanReplaceCallback)
			scanReplaceCallback();

		refreshPluginList();
	};
	addAndMakeVisible(scanReplaceButton);

	scanAddButton.onClick = [this]()
	{
		if (scanAddCallback)
			scanAddCallback();

		refreshPluginList();
	};
	addAndMakeVisible(scanAddButton);

	closeButton.onClick = [this]()
	{
		if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
			dw->exitModalState(0);
		else
			setVisible(false);
	};
	addAndMakeVisible(closeButton);

	refreshPluginList();
}

void PluginScanModal::refreshPluginList()
{
	pluginNames.clear();

	const auto numTypes = pluginManager.knownPluginList.getNumTypes();
	for (int i = 0; i < numTypes; ++i)
	{
		if (auto* desc = pluginManager.knownPluginList.getType(i))
			pluginNames.add(desc->name);
	}

	if (pluginNames.isEmpty())
		pluginNames.add("No plugins available. Click Scan to search.");

	countLabel.setText("Plugins found: " + juce::String(numTypes), juce::dontSendNotification);
	pluginListBox.updateContent();
	repaint();
}

int PluginScanModal::getNumRows()
{
	return pluginNames.size();
}

void PluginScanModal::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
	if (!juce::isPositiveAndBelow(rowNumber, pluginNames.size()))
		return;

	const auto backgroundColour = rowIsSelected ? juce::Colours::deepskyblue.withAlpha(0.25f)
		: findColour(juce::ListBox::backgroundColourId);

	g.setColour(backgroundColour);
	g.fillRoundedRectangle(2.0f, 2.0f, (float)width - 4.0f, (float)height - 4.0f, 4.0f);

	g.setColour(juce::Colours::white);
	g.setFont(14.0f);
	g.drawFittedText(pluginNames[rowNumber], 8, 0, width - 16, height, juce::Justification::centredLeft, 1);
}

void PluginScanModal::resized()
{
	auto bounds = getLocalBounds().reduced(12);
	auto header = bounds.removeFromTop(30);
	auto titleArea = header.removeFromLeft(juce::jmax(150, header.getWidth() - 140));
	titleLabel.setBounds(titleArea);
	countLabel.setBounds(header);

	bounds.removeFromTop(4);
	const int buttonHeight = 28;
	auto footer = bounds.removeFromBottom(buttonHeight);
	const int buttonWidth = 110;
	auto placeButton = [&](juce::TextButton& button)
	{
		button.setBounds(footer.removeFromLeft(buttonWidth).reduced(0, 2));
		footer.removeFromLeft(8);
	};

	placeButton(scanReplaceButton);
	placeButton(scanAddButton);
	closeButton.setBounds(footer.removeFromLeft(buttonWidth).reduced(0, 2));

	bounds.removeFromBottom(8);
	pluginListBox.setBounds(bounds);
}
