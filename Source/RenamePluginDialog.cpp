/*
  ==============================================================================

    RenamePluginDialog.cpp
    Created: 26 Apr 2025 11:44:18pm
    Author:  Ruchirlives

  ==============================================================================
*/

#include "RenamePluginDialog.h"

RenamePluginDialog::RenamePluginDialog(const juce::String& currentPluginInstanceId)
{
    // Set up the ComboBox
    pluginOptions.addItem("Kontakt", 1);
    pluginOptions.addItem("Komplete", 2);
    pluginOptions.setEditableText(true); // Allow the user to edit the text
    pluginOptions.setText(currentPluginInstanceId, juce::dontSendNotification); // Prepopulate with the current ID
    addAndMakeVisible(pluginOptions);
    setSize(400, 300);

    // Set up the OK button
    okButton.setButtonText("OK");
    okButton.onClick = [this]() { closeDialog(true); };
    addAndMakeVisible(okButton);

    // Set up the Cancel button
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() { closeDialog(false); };
    addAndMakeVisible(cancelButton);
}

void RenamePluginDialog::resized()
{
    auto area = getLocalBounds().reduced(10);
    pluginOptions.setBounds(area.removeFromTop(30));
    auto buttonArea = area.removeFromBottom(30);
    okButton.setBounds(buttonArea.removeFromLeft(buttonArea.getWidth() / 2).reduced(5));
    cancelButton.setBounds(buttonArea.reduced(5));
}

juce::String RenamePluginDialog::getSelectedPluginInstanceId() const
{
    return pluginOptions.getText();
}

void RenamePluginDialog::closeDialog(bool accepted)
{
    if (onDialogResult)
        onDialogResult(accepted);
}

