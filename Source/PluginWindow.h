/*
  ==============================================================================

    PluginWindow.h
    Created: 15 Sep 2024 10:33:44pm
    Author:  Desktop

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class PluginWindow : public juce::DocumentWindow
{
public:
	PluginWindow(juce::AudioPluginInstance* pluginInstance);
	~PluginWindow();

	void closeButtonPressed() override;

private:
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};
