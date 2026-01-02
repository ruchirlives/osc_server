/*
  ==============================================================================

    PluginManager.cpp
    Created: 16 Sep 2024 4:03:37pm
    Author:  Desktop

  ==============================================================================
*/

#include "PluginManager.h"
#include "MainComponent.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include "VST3Visitor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <limits>
#include "RenderTimeline.h"

namespace
{
    constexpr std::size_t kMaxTaggedMidiEvents = 50000;
    constexpr juce::uint32 kMidiOverflowLogIntervalMs = 2000;

    std::vector<juce::String> sanitiseTags(const std::vector<juce::String> &tags)
    {
        std::vector<juce::String> cleaned;
        cleaned.reserve(tags.size());

        for (const auto &tag : tags)
        {
            auto t = tag.trim();
            if (t.isEmpty())
                continue;

            auto lowered = t.toLowerCase();
            if (std::find_if(cleaned.begin(), cleaned.end(), [&lowered](const juce::String &existing)
                             { return existing.compareIgnoreCase(lowered) == 0; }) == cleaned.end())
            {
                cleaned.push_back(lowered);
            }
        }

        return cleaned;
    }

    void insertSortedMidiMessage(std::deque<MyMidiMessage> &buffer, MyMidiMessage message)
    {
        if (buffer.empty() || message.timestamp >= buffer.back().timestamp)
        {
            buffer.push_back(std::move(message));
            return;
        }

        auto insertPos = std::upper_bound(buffer.begin(),
                                          buffer.end(),
                                          message.timestamp,
                                          [](juce::int64 stamp, const MyMidiMessage &msg)
                                          {
                                              return stamp < msg.timestamp;
                                          });

        buffer.insert(insertPos, std::move(message));
    }

    juce::String sanitiseRenderName(juce::String s)
    {
        s = s.trim();
        if (s.isEmpty())
            s = "Render";

        const juce::String badChars = "\\/:?\"<>|*";
        for (auto c : badChars)
            s = s.replaceCharacter(c, '_');

        return s.replaceCharacter(' ', '_');
    }

    std::unique_ptr<juce::AudioFormatWriter> createWavWriter(const juce::File &file,
                                                             double sampleRate,
                                                             int numChannels)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        if (stream == nullptr || !stream->openedOk())
            return {};

        auto *raw = wav.createWriterFor(stream.get(),
                                        sampleRate,
                                        (unsigned int)juce::jmax(1, numChannels),
                                        24,
                                        {},
                                        0);
        if (raw == nullptr)
            return {};

        stream.release();
        return std::unique_ptr<juce::AudioFormatWriter>(raw);
    }

    std::unique_ptr<juce::AudioFormatWriter> createFlacWriter(const juce::File &file,
                                                              double sampleRate,
                                                              int numChannels)
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        if (stream == nullptr || !stream->openedOk())
            return {};

        auto *raw = flac.createWriterFor(stream.get(),
                                         sampleRate,
                                         (unsigned int)juce::jmax(1, numChannels),
                                         24,
                                         {},
                                         0);
        if (raw == nullptr)
            return {};

        stream.release();
        return std::unique_ptr<juce::AudioFormatWriter>(raw);
    }
}

HostPlayHead hostPlayHead;
bool PluginManager::playStartIssued = false;
bool PluginManager::midiStartSent = false;

std::unordered_map<juce::String, juce::AudioPluginInstance::HostedParameter *> buildParameterMap(const juce::AudioPluginInstance *pluginInstance)
{
    std::unordered_map<juce::String, juce::AudioPluginInstance::HostedParameter *> parameterMap;
    int numParameters = pluginInstance->getParameters().size();
    for (int i = 0; i < numParameters; ++i)
    {
        juce::AudioPluginInstance::HostedParameter *parameter = pluginInstance->getHostedParameter(i);
        if (parameter != nullptr)
        {
            juce::String parameterID = parameter->getParameterID();
            parameterMap[parameterID] = parameter;
        }
    }

    return parameterMap;
}

void printParameterNames(const juce::AudioPluginInstance *pluginInstance)
{
    auto parameters = pluginInstance->getParameters();
    for (int i = 0; i < parameters.size(); ++i)
    {
        if (auto *parameter = parameters[i])
        {
            juce::String parameterName = parameter->getName(128);
            float parameterValue = parameter->getValue(); // Normalized value (0.0 to 1.0)
            DBG("Parameter " + juce::String(i) + ": " + parameterName + " = " + juce::String(parameterValue));
        }
    }
}

PluginManager::PluginManager(MainComponent *mainComponent, juce::CriticalSection &criticalSection, juce::MidiBuffer &midiBuffer)
    : mainComponent(mainComponent), midiCriticalSection(criticalSection), incomingMidi(midiBuffer)
{
    formatManager.addFormat(new juce::VST3PluginFormat()); // Adds only VST3 format to the format manager
    // Remove: deviceManager.initialise(4, 32, nullptr, true); // Remove this duplicate initialization
    setAudioChannels(4, 32); // Keep only this - it properly initializes the inherited AudioDeviceManager
}

PluginManager::~PluginManager()
{
    shutdownAudio();
}

void PluginManager::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlockExpected;
    liveSampleRateBackup = sampleRate;
    liveBlockSizeBackup = samplesPerBlockExpected;

    int outputChannels = 2;
    if (auto *audioDevice = deviceManager.getCurrentAudioDevice(); audioDevice != nullptr)
    {
        auto activeOutputs = audioDevice->getActiveOutputChannels().countNumberOfSetBits();
        if (activeOutputs > 0)
            outputChannels = activeOutputs;
    }

    rmsDebugIntervalSamples = static_cast<juce::int64>(sampleRate);
    rmsDebugSamplesAccumulated = 0;

    audioRouter.prepare(sampleRate, samplesPerBlockExpected, outputChannels);

    const juce::ScopedLock pluginLock(pluginInstanceLock);
    for (auto &[pluginId, pluginInstance] : pluginInstances)
    {
        if (pluginInstance != nullptr)
        {
            pluginInstance->prepareToPlay(sampleRate, samplesPerBlockExpected);
        }
    }
}

void PluginManager::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
{
    if (renderInProgress.load())
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    // 1) Update the shared play-head before any plugin processes

    auto &pos = hostPlayHead.positionInfo;

    pos = {}; // clear all flags

    // Validate BPM before setting
    if (currentBpm > 0.0)
    {
        pos.setBpm(currentBpm);
    }
    else
    {
        pos.setBpm(120.0); // Default fallback BPM
    }

    pos.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});

    // Validate sample position
    if (playbackSamplePosition >= 0 && currentSampleRate > 0.0)
    {
        pos.setTimeInSamples(playbackSamplePosition);
        pos.setTimeInSeconds(playbackSamplePosition / currentSampleRate);
        pos.setPpqPosition(playbackSamplePosition * (currentBpm / 60.0) / currentSampleRate);
    }
    else
    {
        pos.setTimeInSamples(0);
        pos.setTimeInSeconds(0.0);
        pos.setPpqPosition(0.0);
    }

    pos.setIsPlaying(true);

    // clear the output buffer
    bufferToFill.clearActiveBufferRegion();

    const juce::ScopedLock sl(midiCriticalSection);
    const juce::ScopedLock pluginLock(pluginInstanceLock);

    // Guard against missing audio device
    if (auto *audioDevice = deviceManager.getCurrentAudioDevice(); audioDevice != nullptr)
    {
        double sampleRate = audioDevice->getCurrentSampleRate();

        audioRouter.beginBlock(bufferToFill.numSamples);

        // Purge MIDI messages for non-existent plugins
        taggedMidiBuffer.erase(
            std::remove_if(taggedMidiBuffer.begin(), taggedMidiBuffer.end(),
                           [this](const MyMidiMessage &m)
                           {
                               return pluginInstances.find(m.pluginId) == pluginInstances.end();
                           }),
            taggedMidiBuffer.end());

        const bool isStartingPlayback = (playbackSamplePosition == 0);
        const int graceWindow = bufferToFill.numSamples;
        std::unordered_map<juce::String, juce::MidiBuffer> scheduledPluginMessages;

        if (!taggedMidiBuffer.empty())
        {
            while (!taggedMidiBuffer.empty())
            {
                auto &taggedMessage = taggedMidiBuffer.front();

                if (pluginInstances.find(taggedMessage.pluginId) == pluginInstances.end())
                {
                    taggedMidiBuffer.pop_front();
                    continue;
                }

                bool consumeMessage = false;

                if (sampleRate <= 0.0 || taggedMessage.timestamp == 0)
                {
                    scheduledPluginMessages[taggedMessage.pluginId].addEvent(taggedMessage.message, 0);
                    consumeMessage = true;
                }
                else
                {
                    auto absPos = static_cast<juce::int64>((taggedMessage.timestamp / 1000.0) * sampleRate);
                    auto offset64 = absPos - playbackSamplePosition;
                    const int offset = static_cast<int>(offset64);

                    const bool fitsCurrentBlock = (offset >= 0 && offset < bufferToFill.numSamples);
                    const bool fitsGraceWindow = isStartingPlayback && offset >= -graceWindow && offset < bufferToFill.numSamples;

                    if (fitsCurrentBlock || fitsGraceWindow)
                    {
                        scheduledPluginMessages[taggedMessage.pluginId].addEvent(
                            taggedMessage.message,
                            juce::jlimit(0, bufferToFill.numSamples - 1, offset));
                        // DBG("Scheduling preview event plugin=" << taggedMessage.pluginId
                        //     << " offset=" << offset
                        //     << " blockSamples=" << bufferToFill.numSamples
                        //     << " playbackPos=" << playbackSamplePosition
                        //     << " msg=" << taggedMessage.message.getDescription());
                        consumeMessage = true;
                    }
                    else if (offset < 0)
                    {
                        // Message arrived late, but still deliver it immediately instead of dropping
                        scheduledPluginMessages[taggedMessage.pluginId].addEvent(
                            taggedMessage.message,
                            0);
                        DBG("Scheduling late preview event plugin=" << taggedMessage.pluginId
                                                                    << " offset=" << offset
                                                                    << " msg=" << taggedMessage.message.getDescription());
                        consumeMessage = true;
                    }
                    else if (offset >= bufferToFill.numSamples)
                    {
                        // Queue is sorted by timestamp, so everything beyond this is for a future block
                        break;
                    }
                }

                if (consumeMessage)
                    taggedMidiBuffer.pop_front();
                else
                    break;
            }
        }

        // 2) Process each plugin once, in a single loop
        for (auto &[pluginId, pluginInstance] : pluginInstances)
        {
            if (!pluginInstance)
                continue;

            // Validate plugin instance before processing
            try
            {
                // Check if plugin is properly initialized
                if (pluginInstance->getTotalNumOutputChannels() <= 0)
                {
                    DBG("Warning: Plugin " << pluginId << " has no output channels, skipping");
                    continue;
                }

                // d) prepare a per-plugin tempBuffer with correct channel count
                int numOut = pluginInstance->getTotalNumOutputChannels();
                juce::AudioBuffer<float> tempBuffer(numOut, bufferToFill.numSamples);
                tempBuffer.clear();

                // e) gather tagged MIDI for this plugin
                juce::MidiBuffer matchingMessages;
                if (auto scheduled = scheduledPluginMessages.find(pluginId);
                    scheduled != scheduledPluginMessages.end())
                {
                    matchingMessages = std::move(scheduled->second);
                }

                // merge in live incoming MIDI if this is the selected plugin
                if (pluginId == mainComponent->getOrchestraTableModel().getSelectedPluginId())
                    matchingMessages.addEvents(incomingMidi,
                                               0,
                                               bufferToFill.numSamples,
                                               bufferToFill.startSample);

                // f) run the plugin with error handling
                try
                {
                    pluginInstance->processBlock(tempBuffer, matchingMessages);
                }
                catch (const std::exception &e)
                {
                    DBG("Exception processing plugin " << pluginId << ": " << e.what());
                    tempBuffer.clear(); // Clear buffer to avoid audio artifacts
                    continue;
                }
                catch (...)
                {
                    DBG("Unknown exception processing plugin " << pluginId);
                    tempBuffer.clear(); // Clear buffer to avoid audio artifacts
                    continue;
                }

                audioRouter.routeAudio(pluginId, tempBuffer, bufferToFill.numSamples);

                // g) mix plugin output back into the host buffer
                for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
                {
                    int outCh = ch < tempBuffer.getNumChannels() ? ch : tempBuffer.getNumChannels() - 1;
                    if (outCh >= 0 && outCh < tempBuffer.getNumChannels())
                    {
                        bufferToFill.buffer->addFrom(ch,
                                                     bufferToFill.startSample,
                                                     tempBuffer,
                                                     outCh,
                                                     0,
                                                     bufferToFill.numSamples);
                    }
                }
            }
            catch (const std::exception &e)
            {
                DBG("Exception in plugin processing loop for " << pluginId << ": " << e.what());
                continue;
            }
            catch (...)
            {
                DBG("Unknown exception in plugin processing loop for " << pluginId);
                continue;
            }
        }
    }
    else
    {
        // If the device is missing, clear buffers and skip processing
        bufferToFill.clearActiveBufferRegion();
        incomingMidi.clear();
        return;
    }

    // TAP audio for UDP streaming
    // Tap the final mixed audio buffer once per callback
    if (audioTapCallback != nullptr)
    {
        try
        {
            audioTapCallback(*bufferToFill.buffer);
        }
        catch (const std::exception &e)
        {
            DBG("Exception in audio tap callback: " << e.what());
        }
        catch (...)
        {
            DBG("Unknown exception in audio tap callback");
        }
    }

    // logBusRmsIfNeeded(bufferToFill.numSamples);

    // clear incoming MIDI and advance the host clock
    incomingMidi.clear();
    playbackSamplePosition += bufferToFill.numSamples;
}

void PluginManager::releaseResources()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    for (auto &[pluginId, pluginInstance] : pluginInstances)
    {
        if (pluginInstance != nullptr)
        {
            pluginInstance->releaseResources();
        }
    }
}

void PluginManager::setBpm(double bpm)
{
    currentBpm = bpm;
}

juce::PluginDescription PluginManager::getDescFromName(const juce::String &name)
{
    const auto types = knownPluginList.getTypes();
    for (const auto &desc : types)
    {
        if (desc.name == name)
            return desc;
    }
    return juce::PluginDescription();
}

void PluginManager::instantiatePluginByName(const juce::String &name, const juce::String &pluginId)
{
    juce::PluginDescription desc = getDescFromName(name);
    if (desc.name.isNotEmpty())
    {
        const juce::ScopedLock pluginLock(pluginInstanceLock);
        instantiatePlugin(&desc, pluginId);
    }
    else
    {
        DBG("Plugin not found: " << name);
    }
}

// Method to get plugin instance IDs as a StringArray
juce::StringArray PluginManager::getPluginInstanceIds() const
{
    juce::StringArray instanceIds;
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    for (const auto &pluginPair : pluginInstances)
    {
        instanceIds.add(pluginPair.first);
    }
    return instanceIds;
}

std::vector<PluginManager::PluginInstanceInfo> PluginManager::getPluginInstanceInfos() const
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    std::vector<PluginInstanceInfo> infos;
    infos.reserve(pluginInstances.size());

    for (const auto &pluginPair : pluginInstances)
    {
        PluginInstanceInfo info;
        info.pluginId = pluginPair.first;

        if (const auto *instance = pluginPair.second.get())
            info.pluginName = instance->getName();
        else
            info.pluginName = "Unavailable";

        infos.push_back(std::move(info));
    }

    std::sort(infos.begin(), infos.end(),
              [](const PluginInstanceInfo &a, const PluginInstanceInfo &b)
              {
                  return a.pluginId.compareIgnoreCase(b.pluginId) < 0;
              });

    return infos;
}

void PluginManager::instantiatePlugin(juce::PluginDescription *desc, const juce::String &pluginId)
{
    juce::String errorMessage;

    auto sampleRate = deviceManager.getAudioDeviceSetup().sampleRate;
    auto blockSize = deviceManager.getAudioDeviceSetup().bufferSize;

    std::unique_ptr<juce::AudioPluginInstance> instance = formatManager.createPluginInstance(
        *desc, sampleRate, blockSize, errorMessage);

    if (instance != nullptr)
    {
        const juce::ScopedLock pluginLock(pluginInstanceLock);
        pluginInstances[pluginId] = std::move(instance);
        pluginInstances[pluginId]->setPlayHead(&hostPlayHead);
        pluginInstances[pluginId]->prepareToPlay(sampleRate, blockSize);
        DBG("Plugin instantiated successfully: " << pluginId);
    }
    else
    {
        DBG("Error instantiating plugin: " << errorMessage);
    }
}

void PluginManager::openPluginWindow(juce::String pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginWindows.find(pluginId) == pluginWindows.end() && pluginInstances.find(pluginId) != pluginInstances.end())
    {
        pluginWindows[pluginId] = std::make_unique<PluginWindow>(pluginInstances[pluginId].get());
    }
    else if (pluginWindows.find(pluginId) != pluginWindows.end())
    {
        pluginWindows[pluginId]->setVisible(true);
        getPluginData(pluginId);
    }
    else
    {
        DBG("Plugin window not found: " << pluginId);
        listPluginInstances();
    }
}

void PluginManager::logBusRmsIfNeeded(int numSamples)
{
    if (numSamples <= 0 || rmsDebugIntervalSamples <= 0)
        return;

    rmsDebugSamplesAccumulated += numSamples;
    if (rmsDebugSamplesAccumulated < rmsDebugIntervalSamples)
        return;

    const auto rmsValues = audioRouter.calculateRmsPerBus(numSamples);
    juce::String message("Bus RMS: ");
    for (const auto &[name, rms] : rmsValues)
    {
        message << name << "=" << juce::String(rms, 4) << " ";
    }

    DBG(message.trimEnd());
    rmsDebugSamplesAccumulated = 0;
}

std::vector<PluginManager::StemConfig> PluginManager::getStemConfigs() const
{
    return stemConfigs;
}

void PluginManager::setStemConfigs(const std::vector<StemConfig> &configs)
{
    auto parseRuleLabel = [](const juce::String &label) -> std::vector<juce::String>
    {
        juce::StringArray tokens;
        tokens.addTokens(label, ",", "");
        tokens.trim();
        tokens.removeEmptyStrings();

        std::vector<juce::String> result;
        result.reserve(tokens.size());
        for (const auto &t : tokens)
            result.push_back(t);
        return result;
    };

    std::vector<StemConfig> cleaned;
    cleaned.reserve(configs.size());

    for (const auto &cfg : configs)
    {
        auto stemName = cfg.name.trim();
        if (stemName.isEmpty())
            continue;

        const bool alreadyExists = std::find_if(cleaned.begin(), cleaned.end(),
                                                [&stemName](const StemConfig &other)
                                                { return other.name.compareIgnoreCase(stemName) == 0; }) != cleaned.end();
        if (alreadyExists)
            continue;

        StemConfig dest;
        dest.name = stemName;
        dest.renderEnabled = cfg.renderEnabled;

        for (const auto &rule : cfg.rules)
        {
            auto ruleLabel = rule.label.trim();
            auto ruleTags = !rule.tags.empty() ? rule.tags : parseRuleLabel(ruleLabel);
            auto normalised = sanitiseTags(ruleTags);
            if (normalised.empty())
                continue;

            StemRule cleanedRule;
            juce::StringArray labelTokens;
            for (const auto &t : normalised)
                labelTokens.add(t);

            cleanedRule.label = ruleLabel.isNotEmpty() ? ruleLabel : labelTokens.joinIntoString(", ");
            cleanedRule.tags = normalised;
            dest.rules.push_back(std::move(cleanedRule));
        }

        cleaned.push_back(std::move(dest));
    }

    stemConfigs = cleaned;

    std::vector<AudioRouter::StemRuleDefinition> definitions;
    definitions.reserve(stemConfigs.size());

    for (const auto &stem : stemConfigs)
    {
        AudioRouter::StemRuleDefinition def;
        def.stemName = stem.name;
        for (const auto &rule : stem.rules)
            def.matchRules.push_back(rule.tags);

        definitions.push_back(std::move(def));
    }

    audioRouter.setStemRules(definitions);
}

void PluginManager::rebuildRouterTagIndexFromConductor()
{
    if (mainComponent == nullptr)
        return;

    audioRouter.rebuildTagIndex(mainComponent->getConductor().orchestra);
}

namespace
{
    std::vector<std::string> normaliseRuleTokens(const std::vector<juce::String> &tags)
    {
        std::vector<std::string> out;
        out.reserve(tags.size());

        for (const auto &tag : tags)
        {
            auto trimmed = tag.trim();
            if (trimmed.isEmpty())
                continue;

            out.push_back(trimmed.toLowerCase().toStdString());
        }

        return out;
    }

    std::string normalisePluginId(const juce::String &pluginId)
    {
        auto trimmed = pluginId.trim();
        if (trimmed.isEmpty())
            return {};

        return trimmed.toLowerCase().toStdString();
    }
}

std::vector<std::vector<int>> PluginManager::getStemRuleMatchCounts() const
{
    std::vector<std::vector<int>> counts;
    if (mainComponent == nullptr)
        return counts;

    const auto &orchestra = mainComponent->getConductor().orchestra;
    counts.reserve(stemConfigs.size());

    for (const auto &stem : stemConfigs)
    {
        std::vector<int> ruleCounts(stem.rules.size(), 0);
        std::vector<std::vector<std::string>> normalizedRules;
        normalizedRules.reserve(stem.rules.size());

        for (const auto &rule : stem.rules)
            normalizedRules.push_back(normaliseRuleTokens(rule.tags));

        if (normalizedRules.empty())
        {
            counts.push_back(std::move(ruleCounts));
            continue;
        }

        for (const auto &instrument : orchestra)
        {
            const auto instrumentId = normalisePluginId(instrument.pluginInstanceId);
            if (instrumentId.empty())
                continue;

            for (size_t r = 0; r < normalizedRules.size(); ++r)
            {
                const auto &required = normalizedRules[r];
                if (required.empty())
                    continue;

                const bool matches = std::any_of(required.begin(), required.end(),
                                                 [&instrumentId](const auto &requiredTag)
                                                 {
                                                     return instrumentId.find(requiredTag) != std::string::npos;
                                                 });

                if (matches)
                    ++ruleCounts[r];
            }
        }

        counts.push_back(std::move(ruleCounts));
    }

    return counts;
}

bool PluginManager::saveRoutingConfigToFile(const juce::File &file) const
{
    auto parentDir = file.getParentDirectory();
    if (!parentDir.exists())
        parentDir.createDirectory();

    juce::XmlElement root("RoutingConfig");
    root.setAttribute("version", 1);

    for (const auto &stem : stemConfigs)
    {
        auto *stemElement = root.createNewChildElement("Stem");
        stemElement->setAttribute("name", stem.name);
        stemElement->setAttribute("render", stem.renderEnabled ? 1 : 0);

        for (const auto &rule : stem.rules)
        {
            auto *ruleElement = stemElement->createNewChildElement("Rule");
            ruleElement->setAttribute("label", rule.label);

            for (const auto &tag : rule.tags)
            {
                auto *tagElement = ruleElement->createNewChildElement("Tag");
                tagElement->setAttribute("value", tag);
            }
        }
    }

    return root.writeTo(file);
}

bool PluginManager::loadRoutingConfigFromFile(const juce::File &file)
{
    if (!file.existsAsFile())
        return false;

    juce::XmlDocument doc(file);
    std::unique_ptr<juce::XmlElement> xml(doc.getDocumentElement());

    if (xml == nullptr || !xml->hasTagName("RoutingConfig"))
        return false;

    std::vector<StemConfig> loaded;

    for (auto *stemElement = xml->getFirstChildElement(); stemElement != nullptr; stemElement = stemElement->getNextElement())
    {
        if (!stemElement->hasTagName("Stem"))
            continue;

        auto stemName = stemElement->getStringAttribute("name").trim();
        if (stemName.isEmpty())
            continue;

        StemConfig stem;
        stem.name = stemName;
        stem.renderEnabled = stemElement->getBoolAttribute("render", true);

        for (auto *ruleElement = stemElement->getFirstChildElement(); ruleElement != nullptr; ruleElement = ruleElement->getNextElement())
        {
            if (!ruleElement->hasTagName("Rule"))
                continue;

            StemRule rule;
            rule.label = ruleElement->getStringAttribute("label");

            for (auto *tagElement = ruleElement->getFirstChildElement(); tagElement != nullptr; tagElement = tagElement->getNextElement())
            {
                if (!tagElement->hasTagName("Tag"))
                    continue;

                auto value = tagElement->getStringAttribute("value").trim();
                if (value.isNotEmpty())
                    rule.tags.push_back(value);
            }

            if (!rule.tags.empty())
                stem.rules.push_back(std::move(rule));
        }

        loaded.push_back(std::move(stem));
    }

    setStemConfigs(loaded);
    return true;
}

void PluginManager::instantiateSelectedPlugin(juce::PluginDescription *desc)
{
    juce::Uuid uuid;
    juce::String pluginId = "Selection 1";

    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) == pluginInstances.end())
    {
        instantiatePlugin(desc, pluginId);
    }
    else
    {
        DBG("Plugin already instantiated: " << pluginId);
    }
}

juce::String PluginManager::getPluginData(juce::String pluginId)
{
    juce::String response = "Plugin not found.";
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        juce::PluginDescription desc = pluginInstances[pluginId]->getPluginDescription();
        response = desc.name;
        DBG("Plugin data found: " << response);
    }
    return response;
}

void PluginManager::resetPlugin(const juce::String &pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        // Destroy the plugin window first which also deletes the editor
        pluginWindows.erase(pluginId);

        // Release and reset the plugin instance
        pluginInstances[pluginId]->releaseResources();
        pluginInstances[pluginId].reset();

        // Remove instance entry from map
        pluginInstances.erase(pluginId);

        DBG("Plugin reset: " << pluginId);
    }
}

void PluginManager::resetAllPlugins()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    // Iterate over a copy of the keys, as we modify the original map
    std::vector<juce::String> pluginIds;
    for (const auto &[pluginId, _] : pluginInstances)
    {
        pluginIds.push_back(pluginId);
    }

    for (const auto &pluginId : pluginIds)
    {
        resetPlugin(pluginId);
    }

    pluginInstances.clear();
    pluginWindows.clear();
    DBG("All plugins have been reset.");
}

bool PluginManager::hasPluginInstance(const juce::String &pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        return true;
    }
    return false;
}

void PluginManager::listPluginInstances()
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    for (const auto &[pluginId, pluginInstance] : pluginInstances)
    {
        DBG("Plugin ID: " << pluginId);
        if (pluginInstance != nullptr)
        {
            DBG("Plugin Name: " << pluginInstance->getName());
        }
    }
}

void PluginManager::savePluginData(const juce::String &dataFilePath, const juce::String &filename, const juce::String &pluginId)
{
    // Get plugin unique ID
    juce::String uniqueId = getPluginUniqueId(pluginId);

    // Construct full file path
    juce::String fullFilePath = dataFilePath + "/" + filename + ".vstpreset";

    // Save plugin data to binary file as a .vstpreset
    juce::File dataFile(fullFilePath);

    // Check if the file exists before attempting to clear it
    if (dataFile.existsAsFile())
    {
        dataFile.deleteFile();
    }

    juce::FileOutputStream dataOutputStream(dataFile);

    // Check if the file opened successfully
    if (dataOutputStream.openedOk())
    {
        // Validate that the plugin exists before attempting to use it. Using
        // operator[] here would create a new empty entry and give us a null
        // pointer, so explicitly look up the plugin first.
        const juce::ScopedLock pluginLock(pluginInstanceLock);
        auto pluginIt = pluginInstances.find(pluginId);
        if (pluginIt == pluginInstances.end() || pluginIt->second == nullptr)
        {
            DBG("Failed to save plugin data. Plugin not found: " << pluginId);
            return;
        }

        // Create VST3Visitor instance
        CustomVST3Visitor visitor;

        // Get the plugin instance
        juce::AudioPluginInstance *plugin = pluginIt->second.get();

        // Visit the plugin instance
        plugin->getExtensions(visitor);

        // Get and write the plugin state
        juce::MemoryBlock state = visitor.presetData;

        // Check if the plugin state is empty
        if (state.getSize() == 0)
        {
            DBG("Plugin state is empty.");
            return;
        }

        dataOutputStream.write(state.getData(), state.getSize());
        DBG("Plugin data saved successfully to vstpreset file.");
    }
    else
    {
        DBG("Failed to open file for saving plugin data.");
    }
}

juce::String PluginManager::extractPluginUidFromPreset(const juce::String &dataFilePath, const juce::String &filename)
{
    // Construct full file path
    juce::String fullFilePath = dataFilePath;
    if (!fullFilePath.endsWithChar('/') && !fullFilePath.endsWithChar('\\'))
        fullFilePath += "/";
    fullFilePath += filename;

    // If filename doesn't have .vstpreset extension, add it
    if (!fullFilePath.endsWithIgnoreCase(".vstpreset"))
        fullFilePath += ".vstpreset";

    juce::File presetFile(fullFilePath);

    if (!presetFile.existsAsFile())
    {
        DBG("Error: Preset file not found: " << fullFilePath);
        return {};
    }

    juce::FileInputStream inputStream(presetFile);
    if (!inputStream.openedOk())
    {
        DBG("Error: Failed to open preset file: " << fullFilePath);
        return {};
    }

    // Read VST3 preset header to extract plugin UID
    char header[4];
    inputStream.read(header, 4);

    if (strncmp(header, "VST3", 4) != 0)
    {
        DBG("Error: Invalid VST3 preset format");
        return {};
    }

    // Read version (4 bytes)
    inputStream.readInt();

    // Read Class ID (16 bytes) - VST3 uses ASCII text characters
    // e.g., "VSTSndCs" for Soundcase, stored as raw ASCII bytes
    char classIdChars[17] = {0};
    inputStream.read(classIdChars, 16);

    // Convert the ASCII characters to hex string
    juce::String result = juce::String::toHexString(reinterpret_cast<const unsigned char*>(classIdChars), 16, 0).toUpperCase();
    
    DBG("Extracted plugin Class ID from preset '" << filename << "':");
    DBG("  Class ID (hex): " << result);
    DBG("  Class ID (ASCII): " << juce::String(classIdChars, 16));

    return result;
}

juce::String PluginManager::getPluginClassId(const juce::String &pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    auto it = pluginInstances.find(pluginId);
    if (it == pluginInstances.end() || it->second == nullptr)
    {
        DBG("Plugin instance not found: " << pluginId);
        return {};
    }

    juce::AudioPluginInstance *plugin = it->second.get();

    // Try to get the VST3 Class ID from the plugin
    CustomVST3Visitor visitor;
    plugin->getExtensions(visitor);

    // The Class ID should be embedded in the preset data header
    if (visitor.presetData.getSize() >= 20)
    {
        const unsigned char *data = static_cast<const unsigned char *>(visitor.presetData.getData());
        // Skip "VST3" header (4 bytes) and version (4 bytes) to get to Class ID (16 bytes)
        if (data[0] == 'V' && data[1] == 'S' && data[2] == 'T' && data[3] == '3')
        {
            const unsigned char *classIdBytes = data + 8;
            return juce::String::toHexString(classIdBytes, 16, 0).toUpperCase();
        }
    }

    // Fallback: use the unique identifier
    return plugin->getPluginDescription().createIdentifierString();
}

juce::String PluginManager::getTuidFromPluginList(const juce::String &presetTuid)
{
    // First check the runtime cache
    auto it = vst3TuidCache.find(presetTuid);
    if (it != vst3TuidCache.end())
    {
        DBG("  Found in runtime cache: " << it->second);
        return it->second;
    }

    // Search PluginList.xml for a plugin matching the preset TUID
    juce::File pluginListFile = juce::File("M:/Desktop/Documents/OSCDawServer/PluginList.xml");

    if (!pluginListFile.existsAsFile())
    {
        // Try relative path from executable directory
        pluginListFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getParentDirectory()
                             .getChildFile("PluginList.xml");
    }

    if (!pluginListFile.existsAsFile())
    {
        DBG("Warning: PluginList.xml not found");
        return {};
    }

    auto xmlDoc = juce::XmlDocument::parse(pluginListFile);
    if (xmlDoc == nullptr)
    {
        DBG("Error: Failed to parse PluginList.xml");
        return {};
    }

    // Search for TUID in plugin entries
    for (auto *pluginElement : xmlDoc->getChildIterator())
    {
        if (pluginElement->getTagName() != "PLUGIN")
            continue;

        juce::String pluginName = pluginElement->getStringAttribute("name");

        // First try: check if plugin has tuid attribute (populated by updatePluginListWithTuids)
        juce::String storedTuid = pluginElement->getStringAttribute("tuid");
        if (storedTuid.isNotEmpty() && storedTuid.equalsIgnoreCase(presetTuid))
        {
            DBG("  Found matching plugin in PluginList: " << pluginName << " (tuid: " << storedTuid << ")");
            vst3TuidCache[presetTuid] = pluginName; // Cache it
            return pluginName;
        }

        // Fallback: use uniqueId (last 4 bytes / 8 hex chars of TUID)
        juce::String uniqueId = pluginElement->getStringAttribute("uniqueId");
        if (presetTuid.endsWithIgnoreCase(uniqueId))
        {
            DBG("  Found matching plugin in PluginList (via uniqueId): " << pluginName << " (uniqueId: " << uniqueId << ")");
            vst3TuidCache[presetTuid] = pluginName; // Cache it
            return pluginName;                      // Return plugin name for instantiation
        }
    }

    DBG("  No matching plugin found in PluginList for TUID: " << presetTuid);
    return {};
}

void PluginManager::enrichPluginListWithTuids(juce::XmlElement* pluginListXml)
{
    if (pluginListXml == nullptr)
        return;

    auto sampleRate = deviceManager.getAudioDeviceSetup().sampleRate;
    auto blockSize = deviceManager.getAudioDeviceSetup().bufferSize;
    
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;
    if (blockSize <= 0)
        blockSize = 512;

    DBG("Enriching plugin list with TUIDs...");
    int successCount = 0;
    int failCount = 0;

    for (auto* pluginElement : pluginListXml->getChildIterator())
    {
        if (!pluginElement->hasTagName("PLUGIN"))
            continue;

        juce::String pluginName = pluginElement->getStringAttribute("name");
        juce::String format = pluginElement->getStringAttribute("format");
        
        // Only process VST3 plugins
        if (format != "VST3")
            continue;

        // Skip if TUID already exists
        if (pluginElement->hasAttribute("tuid"))
        {
            DBG("  Skipping " << pluginName << " (TUID already present)");
            continue;
        }

        DBG("  Processing: " << pluginName);

        // Create plugin description from XML element
        juce::PluginDescription desc;
        desc.loadFromXml(*pluginElement);
        
        if (desc.name.isEmpty())
        {
            DBG("    Failed to load description from XML");
            failCount++;
            continue;
        }

        // Try to instantiate the plugin temporarily
        juce::String errorMessage;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        
        try
        {
            // Disable assertions during instantiation - some plugins with copy protection
            // will trigger jasserts when loaded with a debugger present
            juce::ScopedAssertionDisabler disableAsserts;
            instance = formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMessage);
        }
        catch (const std::exception& e)
        {
            DBG("    Exception during instantiation: " << e.what());
            failCount++;
            continue;
        }
        catch (...)
        {
            DBG("    Unknown exception during instantiation");
            failCount++;
            continue;
        }

        if (instance == nullptr)
        {
            DBG("    Failed to instantiate: " << errorMessage);
            failCount++;
            continue;
        }

        // Extract TUID using VST3Visitor
        try
        {
            CustomVST3Visitor visitor;
            instance->getExtensions(visitor);

            if (visitor.presetData.getSize() >= 24)
            {
                const unsigned char* data = static_cast<const unsigned char*>(visitor.presetData.getData());
                
                // Check for VST3 preset header: "VST3"
                if (data[0] == 'V' && data[1] == 'S' && data[2] == 'T' && data[3] == '3')
                {
                    // Skip "VST3" header (4 bytes) and version (4 bytes) to get to Class ID (16 bytes)
                    // Class ID is stored as ASCII characters (e.g., "VSTSndCs" for Soundcase)
                    const unsigned char* classIdBytes = data + 8;
                    
                    // Convert ASCII characters to hex string
                    juce::String tuid = juce::String::toHexString(classIdBytes, 16, 0).toUpperCase();
                    
                    // Add TUID to XML element
                    pluginElement->setAttribute("tuid", tuid);
                    
                    // Update cache
                    vst3TuidCache[tuid] = pluginName;
                    
                    // Show both hex and ASCII representation for debugging
                    juce::String asciiRepresentation = juce::String(reinterpret_cast<const char*>(classIdBytes), 16);
                    DBG("    Success! TUID: " << tuid << " (ASCII: " << asciiRepresentation << ")");
                    successCount++;
                }
                else
                {
                    DBG("    Invalid preset header format");
                    failCount++;
                }
            }
            else
            {
                DBG("    Preset data too small (" << visitor.presetData.getSize() << " bytes)");
                failCount++;
            }
        }
        catch (const std::exception& e)
        {
            DBG("    Exception extracting TUID: " << e.what());
            failCount++;
        }
        catch (...)
        {
            DBG("    Unknown exception extracting TUID");
            failCount++;
        }

        // Clean up instance
        instance.reset();
    }

    DBG("TUID enrichment complete: " << successCount << " succeeded, " << failCount << " failed");
}

bool PluginManager::loadPluginData(const juce::String &dataFilePath, const juce::String &filename, const juce::String &pluginId)
{
    // Construct full file path
    juce::String fullFilePath = dataFilePath;
    if (!fullFilePath.endsWithChar('/') && !fullFilePath.endsWithChar('\\'))
        fullFilePath += "/";
    fullFilePath += filename;

    // If filename doesn't have .vstpreset extension, add it
    if (!fullFilePath.endsWithIgnoreCase(".vstpreset"))
        fullFilePath += ".vstpreset";

    juce::File presetFile(fullFilePath);

    // Check if file exists
    if (!presetFile.existsAsFile())
    {
        DBG("Error: Preset file not found: " << fullFilePath);
        return false;
    }

    // Check if plugin instance exists
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    auto pluginIt = pluginInstances.find(pluginId);
    if (pluginIt == pluginInstances.end() || pluginIt->second == nullptr)
    {
        DBG("Warning: Plugin instance not found: " << pluginId);
        return false;
    }

    juce::FileInputStream inputStream(presetFile);
    if (!inputStream.openedOk())
    {
        DBG("Error: Failed to open preset file: " << fullFilePath);
        return false;
    }

    // Read VST3 preset header
    char header[4];
    inputStream.read(header, 4);

    if (strncmp(header, "VST3", 4) != 0)
    {
        DBG("Error: Invalid VST3 preset format");
        return false;
    }

    // Read version (4 bytes)
    int version = inputStream.readInt();
    DBG("VST3 preset version: " << version);

    // Read Class ID (16 bytes)
    char classId[16];
    inputStream.read(classId, 16);
    juce::String classIdHex = juce::String::toHexString(reinterpret_cast<const unsigned char *>(classId), 16, 0).toUpperCase();
    DBG("Class ID from file: " << classIdHex);

    // Check if there's a size field (4 bytes) for the state data
    int stateDataSize = 0;
    juce::int64 currentPos = inputStream.getPosition();

    // Try reading as little-endian int for state size
    stateDataSize = inputStream.readInt();
    juce::int64 fileSize = presetFile.getSize();
    juce::int64 remainingAfterSize = fileSize - inputStream.getPosition();

    // Validate if this looks like a valid size
    if (stateDataSize > 0 && stateDataSize <= 100 * 1024 * 1024 && stateDataSize == remainingAfterSize)
    {
        // This looks like a valid size field
        DBG("Found size field: " << stateDataSize << " bytes");
    }
    else
    {
        // No size field, treat everything after class ID as state data
        inputStream.setPosition(currentPos);
        stateDataSize = static_cast<int>(fileSize - currentPos);
        DBG("No size field found, reading remaining " << stateDataSize << " bytes as state");
    }

    if (stateDataSize <= 0 || stateDataSize > 100 * 1024 * 1024)
    {
        DBG("Error: Invalid state data size: " << stateDataSize);
        return false;
    }

    juce::MemoryBlock stateData(static_cast<size_t>(stateDataSize));
    inputStream.read(stateData.getData(), stateDataSize);

    // Get the plugin instance
    juce::AudioPluginInstance *plugin = pluginIt->second.get();

    // Verify plugin compatibility by checking UIDs
    juce::String presetPluginUid = extractPluginUidFromPreset(dataFilePath, filename);
    juce::String instancePluginUid = plugin->getPluginDescription().createIdentifierString();

    if (presetPluginUid.isNotEmpty() && !instancePluginUid.contains(presetPluginUid.substring(0, 8)))
    {
        DBG("Warning: Plugin UID mismatch. Preset is for different plugin type.");
        DBG("Preset UID: " << presetPluginUid << ", Instance UID: " << instancePluginUid);
        // Continue anyway - sometimes presets can still work across compatible plugins
    }

    // Use JUCE's built-in method to set the state from the preset data
    DBG("Loading " << stateData.getSize() << " bytes of state data into " << pluginId);
    plugin->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));

    DBG("State information applied to plugin: " << pluginId);
    DBG("Plugin preset loaded successfully: " << fullFilePath << " into " << pluginId);
    return true;
}

juce::String PluginManager::getPluginUniqueId(const juce::String &pluginId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    auto it = pluginInstances.find(pluginId);
    if (it == pluginInstances.end() || it->second == nullptr)
    {
        DBG("Error: Plugin ID not found or plugin instance is null.");
        return "Invalid Plugin ID";
    }

    // Get the plugin instance
    juce::AudioPluginInstance *plugin = it->second.get();

    // Get the plugin description
    juce::PluginDescription description = plugin->getPluginDescription();

    // Create Unique ID
    juce::String uniqueId = description.createIdentifierString();

    return uniqueId;
}

void PluginManager::scanPlugins(juce::FileSearchPath searchPaths, bool replaceExisting)
{
    if (replaceExisting)
        knownPluginList.clear();

    DBG((replaceExisting ? "Scanning (replace) for VST3 plugins in " : "Scanning (add) for VST3 plugins in ") << searchPaths.toString());

    // Create a persistent VST3PluginFormat instance
    juce::VST3PluginFormat vst3Format;

    juce::PluginDirectoryScanner scanner(knownPluginList, vst3Format, searchPaths, true, juce::File(), false);

    juce::String nameOfPluginBeingScanned;
    while (scanner.scanNextFile(true, nameOfPluginBeingScanned))
    {
        juce::Thread::sleep(100);
        if (knownPluginList.getNumTypes() > 50)
        {
            break;
        }
    }

    DBG("Scanning completed. " << knownPluginList.getNumTypes() << " VST3 Plugins Found");
    savePluginListToFile();
    DBG("Plugin list saved to file.");
}

void PluginManager::removePluginsByIndexes(const juce::Array<int> &rowsToRemove)
{
    if (rowsToRemove.isEmpty())
        return;

    std::vector<int> sortedRows;
    sortedRows.reserve(static_cast<size_t>(rowsToRemove.size()));

    for (int i = 0; i < rowsToRemove.size(); ++i)
        sortedRows.push_back(rowsToRemove[i]);

    std::sort(sortedRows.begin(), sortedRows.end(), [](int a, int b)
              { return a > b; });

    const auto snapshot = knownPluginList.getTypes();

    for (int row : sortedRows)
    {
        if (!juce::isPositiveAndBelow(row, snapshot.size()))
            continue;

        knownPluginList.removeType(snapshot.getReference(row));
    }

    savePluginListToFile();
}

void PluginManager::savePluginListToFile()
{
    // Create OSCDawServer subfolder in user's documents directory
    juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OSCDawServer");
    if (!dawServerDir.exists())
        dawServerDir.createDirectory();

    // Use OSCDawServer subfolder for PluginList.xml
    juce::File pluginListFile = dawServerDir.getChildFile("PluginList.xml");

    std::unique_ptr<juce::XmlElement> pluginListXml = knownPluginList.createXml();

    if (pluginListXml != nullptr)
    {
        // Enrich plugin entries with TUIDs by temporarily instantiating each plugin
        enrichPluginListWithTuids(pluginListXml.get());

        if (pluginListFile.existsAsFile())
            pluginListFile.deleteFile();

        bool existed = pluginListFile.existsAsFile();
        pluginListXml->writeTo(pluginListFile);

        if (!existed)
            pluginListFile.setCreationTime(juce::Time::getCurrentTime());

        pluginListFile.setLastModificationTime(juce::Time::getCurrentTime());
        pluginListFile.setLastAccessTime(juce::Time::getCurrentTime());
    }
    else
    {
        DBG("Failed to create XML from plugin list");
    }
}

bool PluginManager::loadPluginListFromFile()
{
    // Create OSCDawServer subfolder in user's documents directory
    juce::File dawServerDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OSCDawServer");
    if (!dawServerDir.exists())
        dawServerDir.createDirectory();

    // Use OSCDawServer subfolder for PluginList.xml
    juce::File pluginListFile = dawServerDir.getChildFile("PluginList.xml");

    if (pluginListFile.existsAsFile())
    {
        std::unique_ptr<juce::XmlElement> pluginListXml = juce::XmlDocument::parse(pluginListFile);

        if (pluginListXml != nullptr)
        {
            knownPluginList.recreateFromXml(*pluginListXml);
            pluginListFile.setLastAccessTime(juce::Time::getCurrentTime());
            return true;
        }
        else
        {
            DBG("Failed to parse XML from PluginList.xml");
            return false;
        }
    }
    else
    {
        DBG("PluginList.xml does not exist");
        return false;
    }
}

void PluginManager::clearTaggedMidiBuffer()
{
    const juce::ScopedLock sl(midiCriticalSection);
    taggedMidiBuffer.clear();
}

void PluginManager::clearMasterTaggedMidiBuffer()
{
    const juce::ScopedLock sl(midiCriticalSection);
    masterTaggedMidiBuffer.clear();
}

void PluginManager::printTaggedMidiBuffer()
{
    const juce::ScopedLock sl(midiCriticalSection);
    DBG("Tagged MIDI Buffer Contents:");
    for (const auto &taggedMessage : taggedMidiBuffer)
    {
        DBG("Plugin ID: " << taggedMessage.pluginId
                          << ", Timestamp: " << taggedMessage.timestamp
                          << ", Message: " << taggedMessage.message.getDescription());
    }
}

void PluginManager::printMasterTaggedMidiBufferSummary()
{
    const juce::ScopedLock sl(midiCriticalSection);
    if (masterTaggedMidiBuffer.empty())
    {
        DBG("Master MIDI capture buffer empty. Recording "
            << (captureEnabled ? "ON" : "OFF"));
        return;
    }

    const auto first = masterTaggedMidiBuffer.front().timestamp;
    const auto last = masterTaggedMidiBuffer.back().timestamp;
    DBG("Master MIDI capture size: " << (int)masterTaggedMidiBuffer.size()
                                     << ", first ts: " << first << "ms, last ts: " << last << "ms, Recording "
                                     << (captureEnabled ? "ON" : "OFF"));
}

void PluginManager::debugPrintMasterTaggedMidiBuffer()
{
    const juce::ScopedLock sl(midiCriticalSection);
    DBG("=== Master Tagged MIDI Buffer Dump (" << masterTaggedMidiBuffer.size() << " events) ===");
    int index = 0;
    for (const auto &entry : masterTaggedMidiBuffer)
    {
        DBG("#" << index++
                << " plugin=" << entry.pluginId
                << " ts(ms)=" << entry.timestamp
                << " msg=" << entry.message.getDescription());
    }
    DBG("=== End of Master Tagged MIDI Buffer ===");
}

void PluginManager::startCapture(double startMs)
{
    const juce::ScopedLock sl(midiCriticalSection);
    masterTaggedMidiBuffer.clear();
    captureStartMs = (startMs >= 0.0) ? startMs : -1.0;
    captureEnabled = true;
    previewActive = false;
    previewPaused = false;
    previewOffsetMs = 0.0;
}

void PluginManager::stopCapture()
{
    const juce::ScopedLock sl(midiCriticalSection);
    captureEnabled = false;
}

bool PluginManager::isCaptureEnabled() const
{
    auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
    const juce::ScopedLock sl(lock);
    return captureEnabled;
}

std::vector<MyMidiMessage> PluginManager::snapshotMasterTaggedMidiBuffer()
{
    const juce::ScopedLock sl(midiCriticalSection);
    return {masterTaggedMidiBuffer.begin(), masterTaggedMidiBuffer.end()};
}

bool PluginManager::hasMasterTaggedMidiData() const
{
    auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
    const juce::ScopedLock sl(lock);
    return !masterTaggedMidiBuffer.empty();
}

double PluginManager::getMasterFirstEventMs() const
{
    auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
    const juce::ScopedLock sl(lock);
    if (masterTaggedMidiBuffer.empty())
        return 0.0;
    return static_cast<double>(masterTaggedMidiBuffer.front().timestamp);
}

bool PluginManager::saveMasterTaggedMidiBufferToFile(const juce::File &file)
{
    auto snapshot = snapshotMasterTaggedMidiBuffer();
    double startMs;
    {
        auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
        const juce::ScopedLock sl(lock);
        startMs = captureStartMs;
    }

    if (snapshot.empty())
        return false;

    juce::XmlElement root("MasterTaggedMidiBuffer");
    root.setAttribute("captureStartMs", startMs);

    for (const auto &event : snapshot)
    {
        auto *xmlEvent = root.createNewChildElement("Event");
        xmlEvent->setAttribute("pluginId", event.pluginId);
        xmlEvent->setAttribute("timestamp", juce::String(static_cast<juce::int64>(event.timestamp)));

        juce::MemoryBlock dataBlock(event.message.getRawData(),
                                    static_cast<size_t>(event.message.getRawDataSize()));
        xmlEvent->setAttribute("data", dataBlock.toBase64Encoding());
    }

    if (auto parent = file.getParentDirectory(); !parent.exists())
        parent.createDirectory();

    return root.writeTo(file);
}

bool PluginManager::loadMasterTaggedMidiBufferFromFile(const juce::File &file)
{
    if (!file.existsAsFile())
        return false;

    juce::XmlDocument doc(file);
    std::unique_ptr<juce::XmlElement> xml(doc.getDocumentElement());
    if (xml == nullptr || !xml->hasTagName("MasterTaggedMidiBuffer"))
        return false;

    std::vector<MyMidiMessage> loaded;
    loaded.reserve(xml->getNumChildElements());

    for (auto *event = xml->getFirstChildElement(); event != nullptr; event = event->getNextElement())
    {
        if (!event->hasTagName("Event"))
            continue;

        const juce::String dataString = event->getStringAttribute("data");
        juce::MemoryBlock dataBlock;
        if (!dataBlock.fromBase64Encoding(dataString) || dataBlock.getSize() == 0)
            continue;

        juce::MidiMessage midiMessage(dataBlock.getData(),
                                      static_cast<int>(dataBlock.getSize()));
        const juce::String pluginId = event->getStringAttribute("pluginId");
        const juce::String timestampString = event->getStringAttribute("timestamp");
        if (timestampString.isEmpty())
            continue;
        const juce::int64 timestamp = timestampString.getLargeIntValue();
        loaded.emplace_back(midiMessage, pluginId, timestamp);
    }

    if (loaded.empty())
        return false;

    double loadedCaptureStart = xml->getDoubleAttribute("captureStartMs", -1.0);
    if (loadedCaptureStart < 0.0 && !loaded.empty())
        loadedCaptureStart = static_cast<double>(loaded.front().timestamp);

    {
        const juce::ScopedLock sl(midiCriticalSection);
        masterTaggedMidiBuffer.clear();
        taggedMidiBuffer.clear();
        previewActive = false;
        previewPaused = false;
        previewOffsetMs = 0.0;
        captureStartMs = loadedCaptureStart;

        for (auto &message : loaded)
            insertSortedMidiMessage(masterTaggedMidiBuffer, std::move(message));
    }

    resetPlayback();
    stopAllNotes();
    return true;
}

juce::String PluginManager::getRenderProjectName() const
{
    if (mainComponent != nullptr)
        return mainComponent->getCurrentProjectName();
    return "Capture";
}

void PluginManager::prepareAllPlugins(double sampleRate, int blockSize)
{
    if (sampleRate <= 0.0 || blockSize <= 0)
        return;

    const juce::ScopedLock pluginLock(pluginInstanceLock);
    for (auto &[pluginId, pluginInstance] : pluginInstances)
    {
        juce::ignoreUnused(pluginId);
        if (pluginInstance != nullptr)
        {
            pluginInstance->prepareToPlay(sampleRate, blockSize);
        }
    }
}

void PluginManager::setRenderProgressCallback(std::function<void(float)> callback)
{
    const juce::ScopedLock sl(renderCallbackLock);
    renderProgressCallback = std::move(callback);
}

void PluginManager::clearRenderProgressCallback()
{
    const juce::ScopedLock sl(renderCallbackLock);
    renderProgressCallback = {};
}

void PluginManager::notifyRenderProgress(float progress)
{
    std::function<void(float)> callback;
    {
        const juce::ScopedLock sl(renderCallbackLock);
        callback = renderProgressCallback;
    }
    if (!callback)
        return;

    juce::MessageManager::callAsync([callback = std::move(callback), progress]()
                                    { callback(progress); });
}

void PluginManager::setRestoreStatusCallback(std::function<void(const juce::String &)> callback)
{
    const juce::ScopedLock sl(restoreStatusLock);
    restoreStatusCallback = std::move(callback);
}

void PluginManager::clearRestoreStatusCallback()
{
    const juce::ScopedLock sl(restoreStatusLock);
    restoreStatusCallback = {};
}

void PluginManager::notifyRestoreStatus(const juce::String &message)
{
    std::function<void(const juce::String &)> callback;
    {
        const juce::ScopedLock sl(restoreStatusLock);
        callback = restoreStatusCallback;
    }
    if (!callback)
        return;

    callback(message);
}

void PluginManager::invokeOnMessageThreadBlocking(std::function<void()> fn)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        fn();
        return;
    }

    juce::WaitableEvent done;
    juce::MessageManager::callAsync([fn = std::move(fn), &done]()
                                    {
            fn();
            done.signal(); });
    done.wait();
}

void PluginManager::beginExclusiveRender(double sampleRate, int blockSize)
{
    jassert(sampleRate > 0.0);
    jassert(blockSize > 0);

    const bool wasRendering = renderInProgress.exchange(true);
    if (wasRendering)
        return;

    liveSampleRateBackup = currentSampleRate;
    liveBlockSizeBackup = currentBlockSize;

    {
        const juce::ScopedLock sl(midiCriticalSection);
        taggedMidiBuffer.clear();
        incomingMidi.clear();
    }

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        prepareAllPlugins(sampleRate, blockSize);
    }
    else
    {
        invokeOnMessageThreadBlocking([this, sampleRate, blockSize]()
                                      { prepareAllPlugins(sampleRate, blockSize); });
    }
    renderProgress.store(0.0f);
}

void PluginManager::endExclusiveRender()
{
    if (!renderInProgress.load())
        return;

    stopAllNotes();

    {
        const juce::ScopedLock sl(midiCriticalSection);
        taggedMidiBuffer.clear();
        incomingMidi.clear();
    }

    if (liveSampleRateBackup > 0.0 && liveBlockSizeBackup > 0)
    {
        currentSampleRate = liveSampleRateBackup;
        currentBlockSize = liveBlockSizeBackup;
        invokeOnMessageThreadBlocking([this]()
                                      { prepareAllPlugins(currentSampleRate, currentBlockSize); });
    }

    renderInProgress.store(false);
    renderProgress.store(0.0f);
}

bool PluginManager::renderMaster(const juce::File &outFolder,
                                 const juce::String &projectName,
                                 int blockSize,
                                 double tailSeconds,
                                 RenderFormatOptions formatOptions)
{
    if (!formatOptions.writeWav && !formatOptions.writeFlac)
    {
        DBG("RenderMaster: no output formats enabled");
        return false;
    }

    juce::File targetFolder = outFolder;
    if (!targetFolder.exists())
        targetFolder.createDirectory();
    if (!targetFolder.isDirectory())
    {
        DBG("RenderMaster: target folder invalid: " << targetFolder.getFullPathName());
        return false;
    }

    double sampleRate = currentSampleRate;
    if (sampleRate <= 0.0)
    {
        if (auto *device = deviceManager.getCurrentAudioDevice())
            sampleRate = device->getCurrentSampleRate();
    }
    if (sampleRate <= 0.0)
    {
        DBG("RenderMaster: invalid sample rate");
        return false;
    }

    if (blockSize <= 0)
        blockSize = currentBlockSize > 0 ? currentBlockSize : 512;

    auto snapshot = snapshotMasterTaggedMidiBuffer();
    if (snapshot.empty())
    {
        DBG("RenderMaster: master capture empty");
        return false;
    }

    const double renderZeroMs = static_cast<double>(snapshot.front().timestamp);
    auto renderEvents = buildRenderTimelineFromSnapshot(snapshot, renderZeroMs, sampleRate);
    if (renderEvents.empty())
    {
        DBG("RenderMaster: render events empty after conversion");
        return false;
    }

    const auto endSample = computeEndSampleWithTail(renderEvents, sampleRate, tailSeconds);
    if (endSample <= 0)
    {
        DBG("RenderMaster: computed endSample <= 0");
        return false;
    }

    std::map<juce::String, std::vector<std::unique_ptr<juce::AudioFormatWriter>>> writers;
    auto addWriterForFormat = [&](const juce::String &busName,
                                  const juce::String &fileSuffix,
                                  std::unique_ptr<juce::AudioFormatWriter> (*factory)(const juce::File &, double, int)) -> bool
    {
        const juce::String busFileName = sanitiseRenderName(projectName) + fileSuffix;
        auto targetFile = targetFolder.getChildFile(busFileName);
        if (targetFile.existsAsFile())
            targetFile.deleteFile();

        auto writer = factory(targetFile, sampleRate, 2);
        if (!writer)
        {
            DBG("RenderMaster: failed to create writer for " << targetFile.getFullPathName());
            return false;
        }

        writers[busName].push_back(std::move(writer));
        return true;
    };
    auto addBusWriters = [&](const juce::String &busName, const juce::String &baseSuffix) -> bool
    {
        bool added = false;
        if (formatOptions.writeWav)
            added = addWriterForFormat(busName, baseSuffix + ".wav", createWavWriter) || added;
        if (formatOptions.writeFlac)
            added = addWriterForFormat(busName, baseSuffix + ".flac", createFlacWriter) || added;
        return added;
    };

    if (!addBusWriters("Master", "_Master"))
        return false;

    for (const auto &stem : stemConfigs)
    {
        if (!stem.renderEnabled)
            continue;

        if (!addBusWriters(stem.name, "_" + sanitiseRenderName(stem.name)))
            return false;
    }

    std::unordered_map<juce::String, juce::MidiBuffer> midiByPlugin;
    size_t eventIndex = 0;
    juce::AudioBuffer<float> pluginBuffer;

    const juce::ScopedLock pluginLock(pluginInstanceLock);
    audioRouter.prepare(sampleRate, blockSize, 2);
    audioRouter.setRenderDebugEnabled(true);
    for (int64 blockStart = 0; blockStart < endSample; blockStart += blockSize)
    {
        const int numSamples = (int)juce::jmin<int64>(blockSize, endSample - blockStart);
        audioRouter.beginBlock(numSamples);
        midiByPlugin.clear();

        const int64 blockEnd = blockStart + numSamples;
        while (eventIndex < renderEvents.size() && renderEvents[eventIndex].samplePos < blockEnd)
        {
            const auto &ev = renderEvents[eventIndex];
            if (ev.samplePos >= blockStart)
            {
                const int offset = (int)(ev.samplePos - blockStart);
                midiByPlugin[ev.pluginId].addEvent(ev.message, offset);
            }
            ++eventIndex;
        }

        for (const auto &[pluginId, pluginInstance] : pluginInstances)
        {
            if (pluginInstance == nullptr)
                continue;

            juce::MidiBuffer midi;
            if (auto it = midiByPlugin.find(pluginId); it != midiByPlugin.end())
                midi = std::move(it->second);

            const int pluginChannels = juce::jmax(1, pluginInstance->getTotalNumOutputChannels());
            pluginBuffer.setSize(pluginChannels, numSamples, false, false, true);
            pluginBuffer.clear();

            try
            {
                pluginInstance->processBlock(pluginBuffer, midi);
            }
            catch (const std::exception &e)
            {
                DBG("RenderMaster: exception processing " << pluginId << ": " << e.what());
                pluginBuffer.clear();
            }
            catch (...)
            {
                DBG("RenderMaster: unknown exception processing " << pluginId);
                pluginBuffer.clear();
            }

            audioRouter.routeAudio(pluginId, pluginBuffer, numSamples);
        }

        for (auto &[busName, writerList] : writers)
        {
            const auto *busBuf = audioRouter.getBusBuffer(busName);
            if (busBuf == nullptr || busBuf->getNumChannels() == 0)
                continue;

            const float *channelPointers[2] = {
                busBuf->getNumChannels() > 0 ? busBuf->getReadPointer(0) : nullptr,
                busBuf->getNumChannels() > 1 ? busBuf->getReadPointer(1) : (busBuf->getNumChannels() > 0 ? busBuf->getReadPointer(0) : nullptr)};

            for (auto &writer : writerList)
            {
                if (writer != nullptr)
                    writer->writeFromFloatArrays(channelPointers, 2, numSamples);
            }
        }
        float progressValue = static_cast<float>(blockStart) / static_cast<float>(endSample);
        renderProgress.store(progressValue);
        notifyRenderProgress(progressValue);
    }

    writers.clear();
    audioRouter.setRenderDebugEnabled(false);
    renderProgress.store(1.0f);
    notifyRenderProgress(1.0f);
    return true;
}

PluginManager::MasterBufferSummary PluginManager::getMasterTaggedMidiSummary() const
{
    auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
    const juce::ScopedLock sl(lock);
    MasterBufferSummary summary;
    summary.totalEvents = masterTaggedMidiBuffer.size();

    if (masterTaggedMidiBuffer.empty())
        return summary;

    const auto firstTimestamp = masterTaggedMidiBuffer.front().timestamp;
    const auto lastTimestamp = masterTaggedMidiBuffer.back().timestamp;
    summary.durationMs = juce::jmax<juce::int64>(0, lastTimestamp - firstTimestamp);

    std::unordered_set<std::string> uniquePlugins;
    uniquePlugins.reserve(masterTaggedMidiBuffer.size());

    for (const auto &message : masterTaggedMidiBuffer)
    {
        uniquePlugins.insert(message.pluginId.toStdString());

        if (message.message.isNoteOn())
            ++summary.noteOnCount;
        else if (message.message.isNoteOff())
            ++summary.noteOffCount;
        else if (message.message.isController())
            ++summary.ccCount;
        else
            ++summary.otherCount;
    }

    summary.uniquePluginCount = static_cast<int>(uniquePlugins.size());
    return summary;
}

void PluginManager::enqueueMasterForPreview(const std::vector<MyMidiMessage> &source,
                                            double offsetMs,
                                            double baseTimestamp)
{
    double playbackStartTimestamp = baseTimestamp + offsetMs;
    if (baseTimestamp < 0.0 && !source.empty())
        playbackStartTimestamp = static_cast<double>(source.front().timestamp) + offsetMs;

    std::vector<MyMidiMessage> staged;
    staged.reserve(source.size());
    for (const auto &message : source)
    {
        if (message.timestamp < playbackStartTimestamp)
            continue;

        MyMidiMessage scheduled = message;
        auto relativeMs = static_cast<juce::int64>(message.timestamp - playbackStartTimestamp);
        if (relativeMs < 0)
            relativeMs = 0;
        scheduled.timestamp = relativeMs;
        staged.push_back(std::move(scheduled));
    }

    {
        const juce::ScopedLock sl(midiCriticalSection);
        taggedMidiBuffer.assign(staged.begin(), staged.end());
    }
    DBG("enqueueMasterForPreview complete, queued events: " << (int)taggedMidiBuffer.size());
}

void PluginManager::previewPlay()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    auto snapshot = snapshotMasterTaggedMidiBuffer();
    if (snapshot.empty())
    {
        DBG("previewPlay: master capture empty, cannot start preview");
        return;
    }

    resetPlayback();

    double baseTimestamp = captureStartMs;
    if (baseTimestamp < 0.0 && !snapshot.empty())
        baseTimestamp = static_cast<double>(snapshot.front().timestamp);

    {
        const juce::ScopedLock sl(midiCriticalSection);
        if (!previewActive)
        {
            previewActive = true;
            previewPaused = false;
            previewOffsetMs = 0.0;
        }
        else if (previewPaused)
        {
            previewPaused = false;
        }

        previewStartHostMs = nowMs;
        playbackSamplePosition = 0;
    }

    enqueueMasterForPreview(snapshot, previewOffsetMs, baseTimestamp);
}

void PluginManager::previewPause()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    bool shouldStop = false;
    {
        const juce::ScopedLock sl(midiCriticalSection);

        if (!previewActive || previewPaused)
            return;

        previewOffsetMs += (nowMs - previewStartHostMs);
        previewPauseHostMs = nowMs;
        previewPaused = true;
        taggedMidiBuffer.clear();
        shouldStop = true;
    }

    if (shouldStop)
        stopAllNotes();
}

void PluginManager::previewStop()
{
    {
        const juce::ScopedLock sl(midiCriticalSection);
        previewActive = false;
        previewPaused = false;
        previewOffsetMs = 0.0;
        taggedMidiBuffer.clear();
    }
    stopAllNotes();
    resetPlayback();
}

double PluginManager::getPreviewPlaybackTimestampMs() const
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
    const juce::ScopedLock sl(lock);
    double baseTimestamp = captureStartMs;
    if (baseTimestamp < 0.0 && !masterTaggedMidiBuffer.empty())
        baseTimestamp = static_cast<double>(masterTaggedMidiBuffer.front().timestamp);
    if (baseTimestamp < 0.0)
        baseTimestamp = 0.0;

    double offsetMs = previewOffsetMs;
    if (previewActive && !previewPaused)
        offsetMs += (nowMs - previewStartHostMs);

    return baseTimestamp + offsetMs;
}

bool PluginManager::isPreviewActive() const
{
    auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
    const juce::ScopedLock sl(lock);
    return previewActive;
}

bool PluginManager::isPreviewPaused() const
{
    auto &lock = const_cast<juce::CriticalSection &>(midiCriticalSection);
    const juce::ScopedLock sl(lock);
    return previewPaused;
}

void PluginManager::addMidiMessage(const juce::MidiMessage &message, const juce::String &pluginId, juce::int64 &adjustedTimestamp)
{
    const bool rendering = renderInProgress.load();
    const juce::ScopedLock sl(midiCriticalSection); // Lock the critical section to ensure thread safety

    // Live OSC plugins sometimes send timestamp 0. Keep playback scheduling as-is (timestamp 0 = immediate),
    // but record capture needs a monotonic clock so we stamp it with wall-clock ms when missing.
    juce::int64 captureTimestamp = adjustedTimestamp;
    if (captureEnabled && captureTimestamp <= 0)
    {
        captureTimestamp = static_cast<juce::int64>(juce::Time::getMillisecondCounterHiRes());
        if (captureStartMs < 0.0 && masterTaggedMidiBuffer.empty())
            captureStartMs = static_cast<double>(captureTimestamp);
    }

    if (rendering)
    {
        if (captureEnabled)
            insertIntoMasterCaptureUnlocked(MyMidiMessage(message, pluginId, captureTimestamp));
        return;
    }

    insertSortedMidiMessage(taggedMidiBuffer, MyMidiMessage(message, pluginId, adjustedTimestamp));

    if (taggedMidiBuffer.size() > kMaxTaggedMidiEvents)
    {
        const auto overflow = taggedMidiBuffer.size() - kMaxTaggedMidiEvents;
        for (std::size_t i = 0; i < overflow && !taggedMidiBuffer.empty(); ++i)
            taggedMidiBuffer.pop_back();

        static juce::uint32 lastOverflowLog = 0;
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastOverflowLog > kMidiOverflowLogIntervalMs)
        {
            DBG("Warning: MIDI queue exceeded " << (int)kMaxTaggedMidiEvents
                                                << " events; dropping " << (int)overflow << " far-future events.");
            lastOverflowLog = now;
        }
    }

    if (captureEnabled)
        insertIntoMasterCaptureUnlocked(MyMidiMessage(message, pluginId, captureTimestamp));
    // DBG("Added MIDI message: " << message.getDescription() << " for pluginId: " << pluginId << " at adjusted time: " << juce::String(adjustedTimestamp));
}

void PluginManager::insertIntoMasterCapture(MyMidiMessage message)
{
    const juce::ScopedLock sl(midiCriticalSection);
    insertIntoMasterCaptureUnlocked(std::move(message));
}

void PluginManager::insertIntoMasterCaptureUnlocked(MyMidiMessage message)
{
    if (masterTaggedMidiBuffer.empty())
        captureStartMs = static_cast<double>(message.timestamp);

    insertSortedMidiMessage(masterTaggedMidiBuffer, std::move(message));
}

void PluginManager::resetPlayback()
{
    playbackSamplePosition = 0;
    hostPlayHead.positionInfo.setIsPlaying(false);
    // Also clear the taggedMidiBuffer under the MIDI lock
    const juce::ScopedLock sl(midiCriticalSection);
    taggedMidiBuffer.clear();
}

// And stop any currently playing notes
void PluginManager::stopAllNotes()
{
    const juce::ScopedLock sl(midiCriticalSection);
    for (auto &[pluginId, pluginInstance] : pluginInstances)
    {
        if (pluginInstance != nullptr)
        {
            int numOut = pluginInstance->getTotalNumOutputChannels();
            if (numOut <= 0)
                continue; // Skip plugins with no output channels

            juce::MidiBuffer stopMessages;
            for (int channel = 1; channel <= 16; ++channel)
            {
                stopMessages.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                stopMessages.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
            }
            juce::AudioBuffer<float> dummyBuffer(numOut, 512);
            dummyBuffer.clear();

            try
            {
                pluginInstance->processBlock(dummyBuffer, stopMessages);
            }
            catch (const std::exception &e)
            {
                DBG("Exception in stopAllNotes for plugin " << pluginId << ": " << e.what());
            }
            catch (...)
            {
                DBG("Unknown exception in stopAllNotes for plugin " << pluginId);
            }
        }
    }
}

juce::int8 PluginManager::getNumInstances(std::vector<juce::String> &instances)
{
    juce::int8 numInstances = 0;

    if (instances.empty())
    {
        const auto safeSize = std::min(pluginInstances.size(), static_cast<size_t>(std::numeric_limits<juce::int8>::max()));
        numInstances = static_cast<juce::int8>(safeSize);
        DBG("Number of total plugins if selection not used: " << juce::String(numInstances));
    }
    else
    {
        // Find the number of instances that are also in the pluginInstances map

        for (const auto &instance : instances)
        {
            if (pluginInstances.find(instance) != pluginInstances.end())
            {
                numInstances++;
                DBG("Instance found: " << instance);
            }
        }
    }
    DBG("Count of instances to save: " << juce::String(numInstances));
    return numInstances;
}

void PluginManager::savePluginDescriptionsToFile(const juce::String &dataFilePath, std::vector<juce::String> instances)
{
    // Save plugin descriptions to binary file paired with the plugin ID
    juce::File dataFile(dataFilePath);

    // Check if the file exists before attempting to clear it
    if (dataFile.existsAsFile())
    {
        dataFile.deleteFile();
    }

    juce::FileOutputStream dataOutputStream(dataFile);

    if (dataOutputStream.openedOk())
    {
        // Save the number of pluginsInstances
        juce::int8 numInstances = getNumInstances(instances);
        dataOutputStream.writeInt(numInstances);

        // Iterate over each plugin instance and save its id and description pair
        for (const auto &pluginPair : pluginInstances)
        {
            // Check if instances is empty or if the pluginId is in the instances vector
            if (instances.empty() || std::find(instances.begin(), instances.end(), pluginPair.first) != instances.end())
            {
                // Write plugin ID
                dataOutputStream.writeString(pluginPair.first);

                // Get and write plugin description
                juce::PluginDescription desc = pluginPair.second->getPluginDescription();
                dataOutputStream.writeString(desc.name);
            }
        }
        DBG("All plugin descriptions saved successfully to binary file.");
    }
    else
    {
        DBG("Failed to open file for saving plugin descriptions.");
    }
}

void PluginManager::restorePluginDescriptionsFromFile(const juce::String &dataFilePath)
{
    // Restore plugin descriptions from binary file dataFilePath
    juce::File dataFile(dataFilePath);
    juce::FileInputStream dataInputStream(dataFile);
    if (dataInputStream.openedOk())
    {
        // Read the number of plugins
        int numPluginInstances = dataInputStream.readInt();

        // iterate plugin instances and remove them
        resetAllPlugins();

        // Iterate over each plugin instance and restore its description
        for (int i = 0; i < numPluginInstances; ++i)
        {
            juce::String pluginId = dataInputStream.readString();
            juce::String name = dataInputStream.readString();
            juce::PluginDescription desc;
            desc.name = name;

            instantiatePluginByName(name, pluginId);
        }

        DBG("All plugin descriptions restored successfully from binary file.");
    }
    else
    {
        DBG("Failed to open file for restoring plugin descriptions.");
    }
}

void PluginManager::upsertPluginDescriptionsFromFile(const juce::String &dataFilePath)
{
    // Restore plugin descriptions from binary file dataFilePath
    juce::File dataFile(dataFilePath);
    juce::FileInputStream dataInputStream(dataFile);
    if (dataInputStream.openedOk())
    {
        // Read the number of plugins
        int numPluginInstances = dataInputStream.readInt();

        // Iterate over each plugin instance and restore its description
        for (int i = 0; i < numPluginInstances; ++i)
        {
            juce::String pluginId = dataInputStream.readString();
            juce::String name = dataInputStream.readString();

            // first check if the pluginId is not already in the pluginInstances map
            if (pluginInstances.find(pluginId) == pluginInstances.end())
            {
                instantiatePluginByName(name, pluginId);
            }

            DBG("All plugin descriptions upserted successfully from binary file.");
        }
    }
    else
    {
        DBG("Failed to open file for restoring plugin descriptions.");
    }
}

juce::MemoryBlock PluginManager::getPluginState(const juce::String &pluginId)
{
    juce::MemoryBlock state;
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        pluginInstances[pluginId]->getStateInformation(state);
    }
    else
    {
        DBG("Plugin not found: " << pluginId);
    }
    return state;
}

void PluginManager::restorePluginState(const juce::String &pluginId, const juce::MemoryBlock &state)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(pluginId) != pluginInstances.end())
    {
        notifyRestoreStatus("Restoring state for plugin: " + pluginId);
        pluginInstances[pluginId]->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        DBG("Plugin state restored for: " << pluginId);
    }
}

void PluginManager::saveAllPluginStates(const juce::String &dataFilePath, std::vector<juce::String> instances)
{
    // Save plugin states to binary file
    juce::File dataFile(dataFilePath);

    // Check if the file exists before attempting to clear it
    if (dataFile.existsAsFile())
    {
        dataFile.deleteFile();
    }

    juce::FileOutputStream dataOutputStream(dataFile);

    if (dataOutputStream.openedOk())
    {
        // Save the number of plugins
        juce::int8 numInstances = getNumInstances(instances);
        DBG("We are sending instances numbering: " << juce::String(numInstances));
        dataOutputStream.writeInt(numInstances);

        // Iterate over each plugin instance and save its state
        const juce::ScopedLock pluginLock(pluginInstanceLock);
        for (const auto &[pluginId, pluginInstance] : pluginInstances)
        {
            // Check if instances is empty or if the pluginId is in the instances vector
            if (instances.empty() || std::find(instances.begin(), instances.end(), pluginId) != instances.end())
            {
                // Write plugin ID
                dataOutputStream.writeString(pluginId);

                // Get and write plugin state
                juce::MemoryBlock state = getPluginState(pluginId);
                dataOutputStream.writeInt((int)state.getSize());
                dataOutputStream.write(state.getData(), state.getSize());
            }
        }
        DBG("All plugin states saved successfully to binary file.");
    }
    else
    {
        DBG("Failed to open file for saving plugin states.");
    }
}

void PluginManager::restoreAllPluginStates(const juce::String &dataFilePath)
{
    // Restore plugin states from binary file
    juce::File dataFile(dataFilePath);
    juce::FileInputStream dataInputStream(dataFile);

    if (dataInputStream.openedOk())
    {
        // Read the number of plugins
        int numPlugins = dataInputStream.readInt();
        DBG("Number of plugins to restore: " << numPlugins);

        for (int i = 0; i < numPlugins; ++i)
        {
            // Read plugin ID
            juce::String pluginId = dataInputStream.readString();

            // Read plugin state size and data
            int stateSize = dataInputStream.readInt();
            juce::MemoryBlock state;
            state.setSize(stateSize);
            dataInputStream.read(state.getData(), stateSize);

            // Restore plugin state
            DBG("Restoring state for plugin: " << pluginId);
            restorePluginState(pluginId, state);
        }
        DBG("All plugin states restored successfully from binary file.");
    }
    else
    {
        DBG("Failed to open file for restoring plugin states.");
    }
}

void PluginManager::renamePluginInstance(const juce::String &oldId, const juce::String &newId)
{
    const juce::ScopedLock pluginLock(pluginInstanceLock);
    if (pluginInstances.find(oldId) != pluginInstances.end())
    {
        // Move the plugin instance to the new ID
        pluginInstances[newId] = std::move(pluginInstances[oldId]);
        pluginInstances.erase(oldId);

        // Update the plugin window mapping if necessary
        if (pluginWindows.find(oldId) != pluginWindows.end())
        {
            pluginWindows[newId] = std::move(pluginWindows[oldId]);
            pluginWindows.erase(oldId);
        }

        DBG("Plugin Instance ID renamed from " + oldId + " to " + newId);
    }
    else
    {
        DBG("Error: Plugin Instance ID " + oldId + " not found.");
    }
}

// Ensure deviceManager is properly initialized and not set to "No Device"
// If you have any code that sets outputDeviceName to "No Device", remove or comment it out.

// ...existing code...
#include <functional>
