/*
  ==============================================================================

    VST3Visitor.cpp
    Created: 18 Nov 2024 10:09:33pm
    Author:  Desktop

  ==============================================================================
*/


#include "VST3Visitor.h"

void CustomVST3Visitor::visitVST3Client(const VST3Client& vst3Client)
{
	// Get preset data
	presetData = vst3Client.getPreset();

	// Get device ID from the presetData memoryblock


}
