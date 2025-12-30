#include "PluginScanModal.h"
#include "PluginManager.h"

namespace
{
static int clampRowToValidRange(int row, int highestValidRow)
{
	return juce::jlimit(0, juce::jmax(0, highestValidRow), row);
}
}

void PluginScanModal::DragSelectHandler::attachTo(juce::ListBox& listBoxRef)
{
	listBox = &listBoxRef;
	anchorRow = -1;
	isDragging = false;
}

void PluginScanModal::DragSelectHandler::detach()
{
	listBox = nullptr;
	anchorRow = -1;
	isDragging = false;
}

int PluginScanModal::DragSelectHandler::getRowAtEventPosition(const juce::MouseEvent& event) const
{
	if (listBox == nullptr)
		return -1;

	auto relative = event.getEventRelativeTo(listBox);
	return listBox->getRowContainingPosition(relative.x, relative.y);
}

void PluginScanModal::DragSelectHandler::mouseDown(const juce::MouseEvent& event)
{
	if (listBox == nullptr)
		return;

	anchorRow = getRowAtEventPosition(event);
	isDragging = event.mods.isLeftButtonDown();
}

void PluginScanModal::DragSelectHandler::mouseDrag(const juce::MouseEvent& event)
{
	if (listBox == nullptr || !isDragging || anchorRow < 0)
		return;

	const int totalRows = listBox->getListBoxModel() != nullptr ? listBox->getListBoxModel()->getNumRows() : 0;
	if (totalRows <= 0)
		return;

	int currentRow = getRowAtEventPosition(event);

	if (currentRow < 0)
	{
		auto relative = event.getEventRelativeTo(listBox);
		const int relativeY = relative.y;
		if (relativeY < 0)
			currentRow = 0;
		else if (relativeY > listBox->getHeight())
			currentRow = totalRows - 1;
		else
			return;
	}

	const int maxRow = totalRows - 1;
	const int start = clampRowToValidRange(juce::jmin(anchorRow, currentRow), maxRow);
	const int end = clampRowToValidRange(juce::jmax(anchorRow, currentRow), maxRow);

	listBox->deselectAllRows();
	listBox->selectRangeOfRows(start, end);
}

void PluginScanModal::DragSelectHandler::mouseUp(const juce::MouseEvent&)
{
	isDragging = false;
	anchorRow = -1;
}

PluginScanModal::PluginScanModal(PluginManager& pluginManagerRef,
	std::function<void()> scanReplaceAction,
	std::function<void()> scanAddAction,
	std::function<void()> listChangedAction)
	: pluginManager(pluginManagerRef),
	scanReplaceCallback(std::move(scanReplaceAction)),
	scanAddCallback(std::move(scanAddAction)),
	listChangedCallback(std::move(listChangedAction))
{
	titleLabel.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(titleLabel);

	countLabel.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(countLabel);

	addAndMakeVisible(pluginListBox);
	pluginListBox.setRowHeight(24);
	pluginListBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
	pluginListBox.setMultipleSelectionEnabled(true);
	pluginListBox.addMouseListener(&dragSelectHandler, true);
	dragSelectHandler.attachTo(pluginListBox);

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

	removeSelectedButton.onClick = [this]()
	{
		removeSelectedPlugins();
	};
	addAndMakeVisible(removeSelectedButton);
	removeSelectedButton.setEnabled(false);

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

PluginScanModal::~PluginScanModal()
{
	pluginListBox.removeMouseListener(&dragSelectHandler);
	dragSelectHandler.detach();
}

void PluginScanModal::refreshPluginList()
{
	pluginNames.clear();

	const auto types = pluginManager.knownPluginList.getTypes();
	const int numTypes = static_cast<int>(types.size());
	for (const auto& desc : types)
	{
		pluginNames.add(desc.name);
	}

	if (pluginNames.isEmpty())
		pluginNames.add("No plugins available. Click Scan to search.");

	countLabel.setText("Plugins found: " + juce::String(numTypes), juce::dontSendNotification);
	pluginListBox.updateContent();
	pluginListBox.deselectAllRows();
	repaint();
	updateActionButtons();
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

void PluginScanModal::selectedRowsChanged(int)
{
	updateActionButtons();
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
	const int buttonWidth = 120;
	auto placeButton = [&](juce::TextButton& button)
	{
		button.setBounds(footer.removeFromLeft(buttonWidth).reduced(0, 2));
		footer.removeFromLeft(8);
	};

	placeButton(scanReplaceButton);
	placeButton(scanAddButton);
	placeButton(removeSelectedButton);
	closeButton.setBounds(footer.removeFromLeft(buttonWidth).reduced(0, 2));

	bounds.removeFromBottom(8);
	pluginListBox.setBounds(bounds);
}

void PluginScanModal::removeSelectedPlugins()
{
	auto selectedRows = pluginListBox.getSelectedRows();
	if (selectedRows.isEmpty() || pluginManager.knownPluginList.getNumTypes() == 0)
		return;

	juce::Array<int> rows;
	for (int i = 0; i < selectedRows.size(); ++i)
		rows.add(selectedRows[i]);

	pluginManager.removePluginsByIndexes(rows);
	refreshPluginList();
	if (listChangedCallback)
		listChangedCallback();
}

void PluginScanModal::updateActionButtons()
{
	const bool hasPlugins = pluginManager.knownPluginList.getNumTypes() > 0;
	const bool hasSelection = pluginListBox.getNumSelectedRows() > 0;
	removeSelectedButton.setEnabled(hasPlugins && hasSelection);
}
