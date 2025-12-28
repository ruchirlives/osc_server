#include "PluginInstancesModal.h"
#include "PluginManager.h"
#include "RenamePluginDialog.h"

PluginInstancesModal::PluginInstancesModal(PluginManager& managerRef)
	: pluginManager(managerRef)
{
	titleLabel.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(titleLabel);

	countLabel.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(countLabel);

	instanceList.setRowHeight(28);
	instanceList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
	addAndMakeVisible(instanceList);

	refreshButton.onClick = [this]()
	{
		refreshInstances();
	};
	addAndMakeVisible(refreshButton);

	closeButton.onClick = [this]()
	{
		if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
			dw->exitModalState(0);
		else
			setVisible(false);
	};
	addAndMakeVisible(closeButton);

	refreshInstances();
}

void PluginInstancesModal::refreshInstances()
{
	instances = pluginManager.getPluginInstanceInfos();
	countLabel.setText("Active: " + juce::String((int)instances.size()), juce::dontSendNotification);
	instanceList.updateContent();
	repaint();
}

int PluginInstancesModal::getNumRows()
{
	return static_cast<int>(instances.size());
}

void PluginInstancesModal::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
	if (!juce::isPositiveAndBelow(rowNumber, instances.size()))
		return;

	const auto backgroundColour = rowIsSelected ? juce::Colours::deepskyblue.withAlpha(0.25f)
		: findColour(juce::ListBox::backgroundColourId);

	g.setColour(backgroundColour);
	g.fillRoundedRectangle(2.0f, 2.0f, (float)width - 4.0f, (float)height - 4.0f, 4.0f);

	const auto& info = instances[(size_t)rowNumber];
	g.setColour(juce::Colours::white);
	g.setFont(14.0f);
	g.drawFittedText(info.pluginId, 8, 2, width - 16, height / 2, juce::Justification::centredLeft, 1);

	g.setFont(12.0f);
	g.setColour(juce::Colours::lightgrey);
	g.drawFittedText(info.pluginName, 8, height / 2, width - 16, height / 2, juce::Justification::centredLeft, 1);
}

void PluginInstancesModal::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
	if (!isPositiveRow(row))
		return;

	if (event.mods.isPopupMenu())
	{
		instanceList.selectRow(row);
		handleContextMenu(row, event);
	}
}

void PluginInstancesModal::resized()
{
	auto bounds = getLocalBounds().reduced(12);
	auto header = bounds.removeFromTop(30);
	titleLabel.setBounds(header.removeFromLeft(bounds.getWidth() * 2 / 3));
	countLabel.setBounds(header);

	bounds.removeFromTop(4);
	const int buttonHeight = 28;
	auto footer = bounds.removeFromBottom(buttonHeight);
	refreshButton.setBounds(footer.removeFromLeft(110).reduced(0, 2));
	footer.removeFromLeft(8);
	closeButton.setBounds(footer.removeFromLeft(110).reduced(0, 2));

	bounds.removeFromBottom(8);
	instanceList.setBounds(bounds);
}

bool PluginInstancesModal::isPositiveRow(int row) const
{
	return juce::isPositiveAndBelow(row, static_cast<int>(instances.size()));
}

void PluginInstancesModal::handleContextMenu(int row, const juce::MouseEvent& event)
{
	if (!isPositiveRow(row))
		return;

	const auto pluginId = instances[(size_t)row].pluginId;

	juce::PopupMenu menu;
	menu.addItem("Rename Plugin Instance", [this, pluginId]() { renameInstance(pluginId); });
	menu.addItem("Purge Plugin Instance", [this, pluginId]() { purgeInstance(pluginId); });

	menu.showMenuAsync(juce::PopupMenu::Options()
		.withTargetComponent(&instanceList)
		.withTargetScreenPosition(event.getScreenPosition()));
}

void PluginInstancesModal::renameInstance(const juce::String& pluginId)
{
	auto* dialog = new RenamePluginDialog(pluginId);

	juce::DialogWindow::LaunchOptions options;
	options.content.setOwned(dialog);
	options.dialogTitle = "Rename Plugin Instance";
	options.dialogBackgroundColour = findColour(juce::ResizableWindow::backgroundColourId);
	options.escapeKeyTriggersCloseButton = true;
	options.useNativeTitleBar = true;
	options.resizable = false;
	options.componentToCentreAround = this;

	if (auto* dw = options.launchAsync())
	{
		dialog->onDialogResult = [this, dialog, dw, pluginId](bool accepted)
		{
			if (accepted)
			{
				auto newId = dialog->getSelectedPluginInstanceId().trim();
				if (newId.isEmpty())
				{
					juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
						"Rename Plugin Instance",
						"Plugin Instance ID cannot be empty.");
					return;
				}
				else if (newId != pluginId)
				{
					if (pluginManager.hasPluginInstance(newId))
					{
						juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
							"Rename Plugin Instance",
							"A plugin with this ID already exists.");
						return;
					}

					pluginManager.renamePluginInstance(pluginId, newId);
				}
				refreshInstances();
			}

			dw->exitModalState(0);
		};
	}
}

void PluginInstancesModal::purgeInstance(const juce::String& pluginId)
{
	const bool shouldPurge = juce::AlertWindow::showOkCancelBox(
		juce::AlertWindow::WarningIcon,
		"Purge Plugin Instance",
		"Remove plugin instance '" + pluginId + "' from memory?",
		"Purge",
		"Cancel",
		this);

	if (shouldPurge)
	{
		pluginManager.resetPlugin(pluginId);
		refreshInstances();
	}
}
