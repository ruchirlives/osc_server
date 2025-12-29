#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <vector>
#include <algorithm>

// Forward declare your struct (or include the header that defines it)
struct InstrumentInfo;

class AudioRouter
{
public:
    AudioRouter() = default;

    struct StemRuleDefinition
    {
        juce::String stemName;
        std::vector<std::vector<juce::String>> matchRules; // each rule is a list of required tags
    };

    // Call once when audio engine starts / sample rate changes
    void prepare(double sampleRate, int maxBlockSize, int numChannels);

    // Call at start of each audio block (audio thread)
    void beginBlock(int numSamples);

    // Call once per rendered plugin buffer (audio thread)
    void routeAudio(const juce::String& pluginInstanceId,
                    const juce::AudioBuffer<float>& pluginAudio,
                    int numSamples);

    // Non-audio thread: rebuild tags lookup from orchestra data
    void rebuildTagIndex(const std::vector<InstrumentInfo>& orchestra);
    void setStemRules(const std::vector<StemRuleDefinition>& stems);

    // Optional: expose buses for downstream recorder/debug (non-audio thread use)
    const juce::AudioBuffer<float>* getBusBuffer(const juce::String& busName) const;
    const std::map<juce::String, juce::AudioBuffer<float>>& getAllBuses() const { return buses; }
    std::map<juce::String, float> calculateRmsPerBus(int numSamples) const;

private:
    using TagSet = std::unordered_set<std::string>;
    struct StemDefinition
    {
        juce::String name;
        std::vector<TagSet> rules;
    };

    // ===== Routing policy (temporary MVP) =====
    // Later we’ll replace this with match rules.
    juce::String chooseStemBusFor(const TagSet& tags) const;

    static TagSet normaliseTags(const std::vector<juce::String>& tags);

    void ensureBusExists(const juce::String& busName);

    void addToBus(const juce::String& busName,
                  const juce::AudioBuffer<float>& src,
                  int numSamples);

private:
    double sr = 0.0;
    int maxBlock = 0;
    int channels = 2;

    // Buses (audio thread writes into these each block)
    std::map<juce::String, juce::AudioBuffer<float>> buses;

    // Tag lookup (rebuilt on non-audio thread, read on audio thread)
    // For MVP we accept eventual consistency: tags update between blocks.
    std::unordered_map<juce::String, TagSet> tagsByPluginId;
    std::vector<StemDefinition> stemDefinitions;
};
