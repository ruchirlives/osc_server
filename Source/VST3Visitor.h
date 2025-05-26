/*
  ==============================================================================

    VST3Visitor.h
    Created: 19 Nov 2024 2:05:09pm
    Author:  Desktop

  ==============================================================================
*/

#pragma once

#include "JuceHeader.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/base/funknown.h"  // For FUID class

// Set namespace
using namespace juce;

// Create an Extensions visitor class for VST3
class CustomVST3Visitor : public ExtensionsVisitor
{
public:
    void visitVST3Client(const VST3Client& vst3Client) override;
    String device_id;
	MemoryBlock presetData;
};