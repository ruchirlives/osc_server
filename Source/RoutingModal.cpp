#include "RoutingModal.h"
#include "PreviewModal.h"
#include <initializer_list>

RoutingModal::RoutingModal(PluginManager& manager)
    : pluginManager(manager), rulesList("rulesList", &rulesModel)
{
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    stemsLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(stemsLabel);

    rulesLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(rulesLabel);

    stemsList.setRowHeight(26);
    stemsList.setMultipleSelectionEnabled(false);
    stemsList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(stemsList);

    rulesList.setRowHeight(26);
    rulesList.setMultipleSelectionEnabled(false);
    rulesList.setModel(&rulesModel);
    rulesList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(rulesList);

    stemNameEditor.setText("New Stem", juce::dontSendNotification);
    addAndMakeVisible(stemNameEditor);

    ruleEditor.setText("strings, long", juce::dontSendNotification);
    addAndMakeVisible(ruleEditor);

    stemNameEditor.onReturnKey = [this]() { renameStem(); };
    stemNameEditor.onEscapeKey = [this]()
    {
        if (juce::isPositiveAndBelow(selectedStem, (int)stems.size()))
            stemNameEditor.setText(stems[(size_t)selectedStem].name, juce::dontSendNotification);
    };
    ruleEditor.onReturnKey = [this]() { addRule(); };
    ruleEditor.onEscapeKey = [this]()
    {
        editingRuleIndex = -1;
        ruleEditor.clear();
    };

    addStemButton.onClick = [this]() { addStem(); };
    saveButton.onClick = [this]() { saveAndApply(); };
    saveXmlButton.onClick = [this]() { saveRoutingToFile(); };
    loadXmlButton.onClick = [this]() { loadRoutingFromFile(); };
    closeButton.onClick = [this]()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
        else
            setVisible(false);
    };
    recordCaptureButton.onClick = [this]()
    {
        pluginManager.startCapture(juce::Time::getMillisecondCounterHiRes());
        statusLabel.setText("Capture recording started.", juce::dontSendNotification);
        updateCaptureControls();
    };
    stopCaptureButton.onClick = [this]()
    {
        pluginManager.stopCapture();
        statusLabel.setText("Capture recording stopped.", juce::dontSendNotification);
        updateCaptureControls();
    };
    debugCaptureButton.onClick = [this]()
    {
        pluginManager.printMasterTaggedMidiBufferSummary();
        pluginManager.debugPrintMasterTaggedMidiBuffer();
    };
    previewButton.onClick = [this]()
    {
        auto summary = pluginManager.getMasterTaggedMidiSummary();
        if (summary.totalEvents == 0)
        {
            statusLabel.setText("No capture data to preview.", juce::dontSendNotification);
            return;
        }

        auto* content = new PreviewModal(pluginManager);
        content->setSize(500, 500);
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Capture Preview";
        options.content.setOwned(content);
        options.dialogBackgroundColour = juce::Colours::black;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    };

    addAndMakeVisible(addStemButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(saveXmlButton);
    addAndMakeVisible(loadXmlButton);
    addAndMakeVisible(closeButton);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(recordCaptureButton);
    addAndMakeVisible(stopCaptureButton);
    addAndMakeVisible(debugCaptureButton);
    addAndMakeVisible(previewButton);
    addAndMakeVisible(captureStatusLabel);

    stems = pluginManager.getStemConfigs();
    refreshed();
    updateCaptureControls();
}

RoutingModal::~RoutingModal()
{
    pluginManager.stopCapture();
}

void RoutingModal::refreshed()
{
    if (stems.empty())
        selectedStem = -1;
    else if (!juce::isPositiveAndBelow(selectedStem, stems.size()))
        selectedStem = 0;

    if (selectedStem >= 0)
        stemsList.selectRow(selectedStem);
    else
        stemsList.deselectAllRows();
    stemsList.updateContent();
    stemsList.repaint();
    refreshRules();
}

void RoutingModal::refreshRules()
{
    updateRuleMatchCounts();

    rulesList.deselectAllRows();
    rulesList.updateContent();
    rulesList.repaint();
}

void RoutingModal::updateRuleMatchCounts()
{
    currentRuleMatchCounts.clear();

    if (!juce::isPositiveAndBelow(selectedStem, stems.size()))
        return;

    const auto counts = pluginManager.getStemRuleMatchCounts();
    if (!juce::isPositiveAndBelow(selectedStem, counts.size()))
        return;

    currentRuleMatchCounts = counts[(size_t)selectedStem];
}

int RoutingModal::getNumRows()
{
    return static_cast<int>(stems.size());
}

void RoutingModal::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (!juce::isPositiveAndBelow(rowNumber, stems.size()))
        return;

    const auto backgroundColour = rowIsSelected ? juce::Colours::darkcyan.withAlpha(0.3f)
        : findColour(juce::ListBox::backgroundColourId);
    const int toggleSize = 16;
    const int toggleX = 8;
    const int toggleY = (height - toggleSize) / 2;

    g.setColour(backgroundColour);
    g.fillRoundedRectangle(toggleX + toggleSize + 4.0f, 2.0f, (float)width - toggleX - toggleSize - 6.0f, (float)height - 4.0f, 4.0f);

    g.setColour(juce::Colours::white);
    g.setFont(14.0f);

    g.setColour(stems[(size_t)rowNumber].renderEnabled ? juce::Colours::green : juce::Colours::darkgrey);
    g.fillRect(toggleX, toggleY, toggleSize, toggleSize);
    g.setColour(juce::Colours::black);
    g.drawRect(toggleX, toggleY, toggleSize, toggleSize, 1);

    g.setColour(juce::Colours::white);
    g.drawFittedText(stems[(size_t)rowNumber].name, toggleX + toggleSize + 12, 0, width - toggleX - toggleSize - 16, height, juce::Justification::centredLeft, 1);
}

void RoutingModal::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    if (!juce::isPositiveAndBelow(row, stems.size()))
        return;

    if (event.mods.isPopupMenu())
    {
        showStemContextMenu(row, event);
        return;
    }

    const int toggleAreaWidth = 24;
    if (event.x < toggleAreaWidth)
    {
        stems[(size_t)row].renderEnabled = !stems[(size_t)row].renderEnabled;
        stemsList.repaintRow(row);
        return;
    }

    stemsList.selectRow(row);
    selectedStem = row;
    refreshRules();
}

void RoutingModal::listBoxItemDoubleClicked(int row, const juce::MouseEvent& /*event*/)
{
    if (!juce::isPositiveAndBelow(row, stems.size()))
        return;

    selectedStem = row;
    stemsList.selectRow(row);
    stemNameEditor.setText(stems[(size_t)row].name, juce::dontSendNotification);
    stemNameEditor.grabKeyboardFocus();
    stemNameEditor.selectAll();
    statusLabel.setText("Editing stem name...", juce::dontSendNotification);
}

void RoutingModal::showStemContextMenu(int row, const juce::MouseEvent& event)
{
    if (!juce::isPositiveAndBelow(row, stems.size()))
        return;

    selectedStem = row;
    stemsList.selectRow(row);
    refreshRules();

    juce::PopupMenu menu;
    menu.addItem(1, "Rename Stem");
    menu.addItem(2, "Remove Stem");
    menu.addSeparator();
    menu.addItem(3, "Add Rule");

    juce::PopupMenu::Options opts;
    opts.withTargetComponent(&stemsList);
    opts.withTargetScreenArea(juce::Rectangle<int>(event.getScreenPosition(), { 1, 1 }));

    const int result = menu.showMenu(opts);
    switch (result)
    {
        case 1:
            stemNameEditor.setText(stems[(size_t)row].name, juce::dontSendNotification);
            stemNameEditor.selectAll();
            stemNameEditor.grabKeyboardFocus();
            statusLabel.setText("Edit the name and press Enter.", juce::dontSendNotification);
            break;
        case 2:
            removeStem();
            break;
        case 3:
            addRule();
            break;
        case 4:
            removeRule();
            break;
        default:
            break;
    }
}

void RoutingModal::showRuleContextMenu(int row, const juce::MouseEvent& event)
{
    if (!juce::isPositiveAndBelow(row, getNumRows()))
        return;

    const int stemRow = stemsList.getSelectedRow();
    if (!juce::isPositiveAndBelow(stemRow, stems.size()))
        return;
    selectedStem = stemRow;

    rulesList.selectRow(row);
    juce::PopupMenu menu;
    menu.addItem(1, "Edit Rule");
    menu.addItem(2, "Remove Rule");

    juce::PopupMenu::Options opts;
    opts.withTargetComponent(&rulesList);
    opts.withTargetScreenArea(juce::Rectangle<int>(event.getScreenPosition(), { 1, 1 }));

    const int result = menu.showMenu(opts);
    if (result == 1)
        startEditingRule(row);
    else if (result == 2)
        removeRule();
}

void RoutingModal::startEditingRule(int row)
{
    if (!juce::isPositiveAndBelow(row, getNumRows()))
        return;

    int stemRow = stemsList.getSelectedRow();
    if (!juce::isPositiveAndBelow(stemRow, stems.size()))
        stemRow = selectedStem;

    if (!juce::isPositiveAndBelow(stemRow, stems.size()))
        return;

    selectedStem = stemRow;
    const auto& rule = stems[(size_t)selectedStem].rules[(size_t)row];
    juce::StringArray tagText;
    for (const auto& tag : rule.tags)
        tagText.add(tag);
    juce::String text = rule.label.isNotEmpty() ? rule.label : tagText.joinIntoString(", ");
    ruleEditor.setText(text, juce::dontSendNotification);
    ruleEditor.grabKeyboardFocus();
    ruleEditor.selectAll();
    editingRuleIndex = row;
    statusLabel.setText("Editing rule - press Enter to save.", juce::dontSendNotification);
}

void RoutingModal::selectedRowsChanged(int lastRowSelected)
{
    juce::ignoreUnused(lastRowSelected);
    selectedStem = stemsList.getSelectedRow();
    refreshRules();
}

void RoutingModal::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    titleLabel.setBounds(bounds.removeFromTop(28));

    auto editors = bounds.removeFromTop(28);
    stemNameEditor.setBounds(editors.removeFromLeft(bounds.getWidth() / 2).reduced(0, 2));
    editors.removeFromLeft(8);
    ruleEditor.setBounds(editors.reduced(0, 2));

    bounds.removeFromTop(6);
    auto listsArea = bounds.removeFromTop(bounds.getHeight() - 120);

    auto leftArea = listsArea.removeFromLeft(listsArea.getWidth() / 2);
    stemsLabel.setBounds(leftArea.removeFromTop(22));
    stemsList.setBounds(leftArea.reduced(0, 4));

    listsArea.removeFromLeft(10);
    rulesLabel.setBounds(listsArea.removeFromTop(22));
    rulesList.setBounds(listsArea.reduced(0, 4));

    const int actionRowHeight = 34;
    const int captureStatusHeight = 24;
    const int statusHeight = 28;
    const int buttonSpacing = 8;

    const int buttonBlockHeight = captureStatusHeight + actionRowHeight * 2 + buttonSpacing + statusHeight + 12;
    auto buttonBlock = bounds.removeFromBottom(buttonBlockHeight);

    auto layoutButtonRow = [&](juce::Rectangle<int> row, std::initializer_list<juce::TextButton*> buttons)
    {
        const int count = static_cast<int>(buttons.size());
        if (count == 0)
            return;

        const int totalSpacing = buttonSpacing * (count - 1);
        const int buttonWidth = std::max(1, (row.getWidth() - totalSpacing) / count);
        int index = 0;
        for (auto it = buttons.begin(); it != buttons.end(); ++it, ++index)
        {
            (*it)->setBounds(row.removeFromLeft(buttonWidth));
            if (index < count - 1)
                row.removeFromLeft(buttonSpacing);
        }
    };

    auto captureStatusArea = buttonBlock.removeFromTop(captureStatusHeight);
    captureStatusLabel.setBounds(captureStatusArea.reduced(4));

    buttonBlock.removeFromTop(6);
    auto row1 = buttonBlock.removeFromTop(actionRowHeight);
    layoutButtonRow(row1, { &addStemButton, &recordCaptureButton, &stopCaptureButton,
                            &debugCaptureButton, &previewButton });

    buttonBlock.removeFromTop(buttonSpacing);
    auto row2 = buttonBlock.removeFromTop(actionRowHeight);
    layoutButtonRow(row2, { &saveButton, &saveXmlButton, &loadXmlButton, &closeButton });

    buttonBlock.removeFromTop(6);
    auto statusArea = buttonBlock.removeFromTop(statusHeight);
    statusLabel.setBounds(statusArea.reduced(4));
}

void RoutingModal::addStem()
{
    auto name = stemNameEditor.getText().trim();
    if (name.isEmpty())
    {
        statusLabel.setText("Stem name cannot be empty.", juce::dontSendNotification);
        return;
    }

    stems.push_back({ name, {} });
    selectedStem = static_cast<int>(stems.size()) - 1;
    stemsList.selectRow(selectedStem);
    refreshed();
}

void RoutingModal::removeStem()
{
    if (!juce::isPositiveAndBelow(selectedStem, stems.size()))
        return;

    stems.erase(stems.begin() + selectedStem);
    selectedStem = juce::jlimit(-1, static_cast<int>(stems.size()) - 1, selectedStem);
    refreshed();
}

void RoutingModal::renameStem()
{
    if (!juce::isPositiveAndBelow(selectedStem, stems.size()))
        return;

    auto name = stemNameEditor.getText().trim();
    if (name.isEmpty())
        return;

    stems[(size_t)selectedStem].name = name;
    refreshed();
}

void RoutingModal::addRule()
{
    if (!juce::isPositiveAndBelow(selectedStem, stems.size()))
        return;

    auto tags = parseTags(ruleEditor.getText());
    if (tags.empty())
    {
        statusLabel.setText("Add at least one tag for a rule.", juce::dontSendNotification);
        return;
    }

    PluginManager::StemRule rule;
    rule.label = ruleEditor.getText().trim();
    rule.tags = tags;
    auto& rules = stems[(size_t)selectedStem].rules;
    if (editingRuleIndex >= 0 && juce::isPositiveAndBelow(editingRuleIndex, rules.size()))
    {
        rules[(size_t)editingRuleIndex] = std::move(rule);
        statusLabel.setText("Rule updated.", juce::dontSendNotification);
        editingRuleIndex = -1;
    }
    else
    {
        rules.push_back(std::move(rule));
        statusLabel.setText("Rule added.", juce::dontSendNotification);
    }
    refreshRules();
    ruleEditor.clear();
}

void RoutingModal::removeRule()
{
    if (!juce::isPositiveAndBelow(selectedStem, stems.size()))
        return;

    int ruleIndex = getSelectedRule();
    if (!juce::isPositiveAndBelow(ruleIndex, stems[(size_t)selectedStem].rules.size()))
        return;

    stems[(size_t)selectedStem].rules.erase(stems[(size_t)selectedStem].rules.begin() + ruleIndex);
    refreshRules();
    if (editingRuleIndex == ruleIndex)
        editingRuleIndex = -1;
}

void RoutingModal::saveAndApply()
{
    pluginManager.setStemConfigs(stems);
    pluginManager.rebuildRouterTagIndexFromConductor();
    stems = pluginManager.getStemConfigs(); // reload after sanitisation
    statusLabel.setText("Routing updated.", juce::dontSendNotification);
    refreshed();
}

void RoutingModal::updateCaptureControls()
{
    const bool recording = pluginManager.isCaptureEnabled();
    recordCaptureButton.setEnabled(!recording);
    stopCaptureButton.setEnabled(recording);
    captureStatusLabel.setText(recording ? "Recording: ON" : "Recording: OFF",
        juce::dontSendNotification);
    if (recordButtonDefaultColour == juce::Colour())
        recordButtonDefaultColour = recordCaptureButton.findColour(juce::TextButton::buttonColourId);
    const juce::Colour highlight = recording ? juce::Colours::crimson : recordButtonDefaultColour;
    recordCaptureButton.setColour(juce::TextButton::buttonColourId, highlight);

    const auto summary = pluginManager.getMasterTaggedMidiSummary();
    previewButton.setEnabled(summary.totalEvents > 0);
}

void RoutingModal::saveRoutingToFile()
{
    juce::FileChooser chooser("Save Routing XML", juce::File(), "*.xml");
    if (chooser.browseForFileToSave(true))
    {
        auto file = chooser.getResult().withFileExtension(".xml");
        if (pluginManager.saveRoutingConfigToFile(file))
            statusLabel.setText("Routing saved to " + file.getFileName(), juce::dontSendNotification);
        else
            statusLabel.setText("Failed to save routing XML.", juce::dontSendNotification);
    }
}

void RoutingModal::loadRoutingFromFile()
{
    juce::FileChooser chooser("Load Routing XML", juce::File(), "*.xml");
    if (chooser.browseForFileToOpen())
    {
        auto file = chooser.getResult();
        if (pluginManager.loadRoutingConfigFromFile(file))
        {
            pluginManager.rebuildRouterTagIndexFromConductor();
            stems = pluginManager.getStemConfigs();
            statusLabel.setText("Routing loaded from " + file.getFileName(), juce::dontSendNotification);
            refreshed();
        }
        else
        {
            statusLabel.setText("Failed to load routing XML.", juce::dontSendNotification);
        }
    }
}

std::vector<juce::String> RoutingModal::parseTags(const juce::String& text) const
{
    juce::StringArray tokens;
    tokens.addTokens(text, ",", "");
    tokens.trim();
    tokens.removeEmptyStrings();

    std::vector<juce::String> result;
    result.reserve(tokens.size());
    for (const auto& t : tokens)
        result.push_back(t.trim());
    return result;
}

int RoutingModal::getSelectedRule() const
{
    return rulesList.getSelectedRow();
}

int RoutingModal::RulesListModel::getNumRows()
{
    if (!juce::isPositiveAndBelow(owner.selectedStem, owner.stems.size()))
        return 0;
    return static_cast<int>(owner.stems[(size_t)owner.selectedStem].rules.size());
}

void RoutingModal::RulesListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (!juce::isPositiveAndBelow(owner.selectedStem, owner.stems.size()))
        return;
    const auto& rules = owner.stems[(size_t)owner.selectedStem].rules;
    if (!juce::isPositiveAndBelow(rowNumber, rules.size()))
        return;

    const auto backgroundColour = rowIsSelected ? juce::Colours::darkorange.withAlpha(0.35f)
        : juce::Colours::transparentBlack;
    g.setColour(backgroundColour);
    g.fillRoundedRectangle(2.0f, 2.0f, (float)width - 4.0f, (float)height - 4.0f, 4.0f);

    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    juce::StringArray tagText;
    for (const auto& t : rules[(size_t)rowNumber].tags)
        tagText.add(t);

    const auto text = rules[(size_t)rowNumber].label.isNotEmpty() ? rules[(size_t)rowNumber].label
        : tagText.joinIntoString(", ");

    const int countWidth = juce::jmin(width / 3, 96);
    const int textWidth = juce::jmax(0, width - countWidth - 16);
    g.drawFittedText(text, 8, 0, textWidth, height, juce::Justification::centredLeft, 1);

    int matchCount = 0;
    if (juce::isPositiveAndBelow(rowNumber, owner.currentRuleMatchCounts.size()))
        matchCount = owner.currentRuleMatchCounts[rowNumber];

    juce::String countText = juce::String(matchCount);
    if (matchCount == 1)
        countText << " match";
    else
        countText << " matches";

    g.setColour(juce::Colours::lightgreen.withAlpha(0.9f));
    g.drawFittedText(countText, width - countWidth - 8, 0, countWidth, height, juce::Justification::centredRight, 1);
}

void RoutingModal::RulesListModel::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        owner.showRuleContextMenu(row, event);
        return;
    }

    if (!juce::isPositiveAndBelow(row, getNumRows()))
        return;

    owner.rulesList.selectRow(row);
}

void RoutingModal::RulesListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent& event)
{
    if (!juce::isPositiveAndBelow(row, getNumRows()))
        return;

    owner.startEditingRule(row);
}
