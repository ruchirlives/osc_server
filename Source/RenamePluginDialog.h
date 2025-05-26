/*
  ==============================================================================

    RenamePluginDialog.h
    Created: 26 Apr 2025 11:44:18pm
    Author:  Ruchirlives

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class RenamePluginDialog : public juce::Component
{
public:
    RenamePluginDialog(const juce::String& currentPluginInstanceId);
    juce::String getSelectedPluginInstanceId() const;

    std::function<void(bool)> onDialogResult;

private:
    void closeDialog(bool accepted);

    juce::ComboBox pluginOptions;
    juce::TextButton okButton, cancelButton;

    void resized() override;
};

