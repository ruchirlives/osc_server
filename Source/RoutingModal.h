#pragma once

#include <JuceHeader.h>
#include "PluginManager.h"

class RoutingModal : public juce::Component,
                     private juce::ListBoxModel
{
public:
    explicit RoutingModal(PluginManager& manager);
    ~RoutingModal() override;

private:
    class RulesListModel : public juce::ListBoxModel
    {
    public:
        explicit RulesListModel(RoutingModal& ownerRef) : owner(ownerRef) {}

        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& event) override;

    private:
        RoutingModal& owner;
    };

    void refreshed();
    void refreshRules();
    void resized() override;
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void addStem();
    void removeStem();
    void renameStem();
    void addRule();
    void removeRule();
    void saveAndApply();
    void saveRoutingToFile();
    void loadRoutingFromFile();
    void updateCaptureControls();
    std::vector<juce::String> parseTags(const juce::String& text) const;
    int getSelectedRule() const;

    PluginManager& pluginManager;
    std::vector<PluginManager::StemConfig> stems;
    int selectedStem = -1;

    juce::Label titleLabel{ "titleLabel", "Routing Setup" };
    juce::Label stemsLabel{ "stemsLabel", "Stems" };
    juce::Label rulesLabel{ "rulesLabel", "Match Rules (comma-separated tags)" };
    juce::Label statusLabel;

    juce::ListBox stemsList{ "stemsList", this };
    RulesListModel rulesModel{ *this };
    juce::ListBox rulesList;

    juce::TextEditor stemNameEditor;
    juce::TextEditor ruleEditor;

    juce::TextButton addStemButton{ "Add Stem" };
    juce::TextButton removeStemButton{ "Remove Stem" };
    juce::TextButton renameStemButton{ "Rename Stem" };
    juce::TextButton addRuleButton{ "Add Rule" };
    juce::TextButton removeRuleButton{ "Remove Rule" };
    juce::TextButton saveButton{ "Save" };
    juce::TextButton saveXmlButton{ "Save XML" };
    juce::TextButton loadXmlButton{ "Load XML" };
    juce::TextButton closeButton{ "Close" };
    juce::TextButton recordCaptureButton{ "Record" };
    juce::TextButton stopCaptureButton{ "Stop" };
    juce::TextButton debugCaptureButton{ "Debug" };
    juce::TextButton previewButton{ "Preview" };
    juce::Label captureStatusLabel{ "captureStatusLabel", "Recording: OFF" };
};
