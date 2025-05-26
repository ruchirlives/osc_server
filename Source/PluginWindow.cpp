/*
  ==============================================================================

    PluginWindow.cpp
    Created: 15 Sep 2024 10:33:44pm
    Author:  Desktop

  ==============================================================================
*/

#include "PluginWindow.h"

PluginWindow::PluginWindow(juce::AudioPluginInstance* pluginInstance)
	: juce::DocumentWindow(pluginInstance->getName(), juce::Colours::lightgrey, juce::DocumentWindow::allButtons)
{
	if (auto* editor = pluginInstance->createEditorIfNeeded())
	{
		setContentOwned(editor, true);
		setResizable(true, true);
		setVisible(true);
		centreWithSize(getWidth(), getHeight());
	}
	else
	{
		setContentOwned(new juce::Label("no-editor", "This plugin has no editor"), true);
	}
	setVisible(true);
}

PluginWindow::~PluginWindow()
{

}

void PluginWindow::closeButtonPressed()
{
	PluginWindow::setVisible(false);
}
