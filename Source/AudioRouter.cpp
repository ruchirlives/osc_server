#include "AudioRouter.h"

// Include the header where InstrumentInfo is defined
// It contains: pluginInstanceId and tags  [oai_citation:3‡Conductor.h](file-service://file-LpDXG54mpw8MSGPQWWDu9E)
#include "Conductor.h" // or wherever InstrumentInfo lives
#include <cmath>

void AudioRouter::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    jassert(sampleRate > 0.0);
    jassert(maxBlockSize > 0);
    jassert(numChannels > 0);

    sr = sampleRate;
    maxBlock = maxBlockSize;
    channels = numChannels;

    buses.clear();
    ensureBusExists("Master");
    for (const auto& stem : stemDefinitions)
        ensureBusExists(stem.name);
}

void AudioRouter::beginBlock(int numSamples)
{
    jassert(numSamples > 0);
    jassert(numSamples <= maxBlock);

    // Ensure all bus buffers have correct shape then clear only this block region
    for (auto& [name, buf] : buses)
    {
        if (buf.getNumChannels() != channels || buf.getNumSamples() != maxBlock)
            buf.setSize(channels, maxBlock, false, true, true);

        buf.clear(0, numSamples);
    }
}

void AudioRouter::routeAudio(const juce::String& pluginInstanceId,
                            const juce::AudioBuffer<float>& pluginAudio,
                            int numSamples)
{
    if (numSamples <= 0) return;

    // Always to Master
    addToBus("Master", pluginAudio, numSamples);

    // Route to one stem bus (MVP)
    auto it = tagsByPluginId.find(pluginInstanceId);

    const TagSet empty;
    const TagSet& tags = (it != tagsByPluginId.end()) ? it->second : empty;

    const auto stem = chooseStemBusFor(tags);
    if (stem.isNotEmpty() && stem != "Master")
        addToBus(stem, pluginAudio, numSamples);
}

void AudioRouter::rebuildTagIndex(const std::vector<InstrumentInfo>& orchestra)
{
    // Build a fresh map then swap (avoid mutating the live map in-place)
    std::unordered_map<juce::String, TagSet> fresh;

    for (const auto& inst : orchestra)
    {
        // multiple orchestra rows can share the same pluginInstanceId
        // so union all tags for that plugin
        auto& setRef = fresh[inst.pluginInstanceId];
        TagSet normal = normaliseTags(inst.tags);
        setRef.insert(normal.begin(), normal.end());
    }

    tagsByPluginId.swap(fresh);
}

void AudioRouter::setStemRules(const std::vector<StemRuleDefinition>& stems)
{
    std::vector<StemDefinition> normalised;
    normalised.reserve(stems.size());
    std::unordered_set<std::string> desiredNames;

    for (const auto& stem : stems)
    {
        if (stem.stemName.isEmpty())
            continue;

        StemDefinition def;
        def.name = stem.stemName;
        for (const auto& ruleTags : stem.matchRules)
            def.rules.push_back(normaliseTags(ruleTags));

        normalised.push_back(std::move(def));
        desiredNames.insert(stem.stemName.toLowerCase().toStdString());
        ensureBusExists(stem.stemName);
    }

    for (auto it = buses.begin(); it != buses.end();)
    {
        const auto lower = it->first.toLowerCase().toStdString();
        if (lower != "master" && desiredNames.find(lower) == desiredNames.end())
            it = buses.erase(it);
        else
            ++it;
    }

    stemDefinitions.swap(normalised);
}

const juce::AudioBuffer<float>* AudioRouter::getBusBuffer(const juce::String& busName) const
{
    auto it = buses.find(busName);
    if (it == buses.end()) return nullptr;
    return &it->second;
}

std::map<juce::String, float> AudioRouter::calculateRmsPerBus(int numSamples) const
{
    std::map<juce::String, float> rmsValues;
    if (numSamples <= 0)
        return rmsValues;

    for (const auto& [name, buf] : buses)
    {
        const int channelsToUse = buf.getNumChannels();
        const int samplesToUse = juce::jmin(numSamples, buf.getNumSamples());

        if (channelsToUse == 0 || samplesToUse == 0)
        {
            rmsValues[name] = 0.0f;
            continue;
        }

        double sumSquares = 0.0;
        for (int ch = 0; ch < channelsToUse; ++ch)
        {
            const float* data = buf.getReadPointer(ch);
            for (int i = 0; i < samplesToUse; ++i)
                sumSquares += static_cast<double>(data[i]) * static_cast<double>(data[i]);
        }

        const double meanSquare = sumSquares / static_cast<double>(channelsToUse * samplesToUse);
        rmsValues[name] = static_cast<float>(std::sqrt(meanSquare));
    }

    return rmsValues;
}

AudioRouter::TagSet AudioRouter::normaliseTags(const std::vector<juce::String>& tags)
{
    TagSet out;
    out.reserve(tags.size());

    for (const auto& t : tags)
    {
        auto s = t.trim().toLowerCase().toStdString();
        if (!s.empty())
            out.insert(std::move(s));
    }
    return out;
}

juce::String AudioRouter::chooseStemBusFor(const TagSet& tags) const
{
    for (const auto& stem : stemDefinitions)
    {
        for (const auto& rule : stem.rules)
        {
            if (rule.empty())
                continue;

            const bool matches = std::all_of(rule.begin(), rule.end(),
                [&tags](const auto& required)
                {
                    return tags.find(required) != tags.end();
                });

            if (matches)
                return stem.name;
        }
    }

    // MVP: simple first-match heuristic (replace with match rules later)
    // Priority order example:
    if (tags.count("fx"))      return "FX";
    if (tags.count("choir"))   return "Choir";
    if (tags.count("brass"))   return "Brass";
    if (tags.count("strings")) return "Strings";
    if (tags.count("perc") || tags.count("drums")) return "Percussion";

    return "Master";
}

void AudioRouter::ensureBusExists(const juce::String& busName)
{
    auto it = buses.find(busName);
    if (it != buses.end())
        return;

    juce::AudioBuffer<float> buf;
    buf.setSize(channels, maxBlock > 0 ? maxBlock : 1, false, true, true);
    buf.clear();

    buses.emplace(busName, std::move(buf));
}

void AudioRouter::addToBus(const juce::String& busName,
                           const juce::AudioBuffer<float>& src,
                           int numSamples)
{
    ensureBusExists(busName);

    auto& dst = buses[busName];

    const int copyChannels = juce::jmin(dst.getNumChannels(), src.getNumChannels());
    for (int ch = 0; ch < copyChannels; ++ch)
        dst.addFrom(ch, 0, src, ch, 0, numSamples);

    // If src is mono, duplicate into remaining channels (optional but handy)
    if (src.getNumChannels() == 1 && dst.getNumChannels() >= 2)
        dst.addFrom(1, 0, src, 0, 0, numSamples);
}
