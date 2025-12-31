#pragma once

#include <JuceHeader.h>

class AudioUdpStreamer
{
public:
	AudioUdpStreamer(const juce::String& ip, int port)
		: targetIp(ip), targetPort(port)
	{
		socket = std::make_unique<juce::DatagramSocket>();
	}

	void sendAudio(const juce::AudioBuffer<float>& buffer)
	{
		const int numSamples = buffer.getNumSamples();
		const int numChannels = buffer.getNumChannels();
		tempBuffer.setSize(numChannels, numSamples, false, false, true);
		tempBuffer.makeCopyOf(buffer);

		std::vector<uint8_t> bytes;
		bytes.reserve(numSamples * numChannels * 2); // 2 bytes per sample

		for (int i = 0; i < numSamples; ++i)
		{
			for (int ch = 0; ch < numChannels; ++ch)
			{
				float sample = tempBuffer.getSample(ch, i);
				int16_t s16 = static_cast<int16_t>(juce::jlimit(-1.0f, 1.0f, sample) * 32767);
				bytes.push_back(static_cast<uint8_t>(s16 & 0xFF));
				bytes.push_back(static_cast<uint8_t>((s16 >> 8) & 0xFF));
			}
		}

		socket->write(targetIp, targetPort, bytes.data(), static_cast<int>(bytes.size()));
	}

	void setPort(int port)
	{
		targetPort = port;
		if (socket)
		{
			socket->bindToPort(targetPort);
		}
		else
		{
			socket = std::make_unique<juce::DatagramSocket>();
			socket->bindToPort(targetPort);
		}
	}

private:
	juce::String targetIp;
	int targetPort;
	std::unique_ptr<juce::DatagramSocket> socket;
	juce::AudioBuffer<float> tempBuffer;
};
