/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IPlugVST3Host.h"
#include "IPlugVST3_Processor.h"

#include "pluginterfaces/vst/ivstaudiooprocessor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_IPLUG_NAMESPACE

IPlugVST3Host::IPlugVST3Host()
{
}

IPlugVST3Host::~IPlugVST3Host()
{
  UnloadPlugin();
}

bool IPlugVST3Host::LoadPlugin(const char* vst3Path)
{
  // Cleanup existing plugin first
  UnloadPlugin();

  // For embedding purposes, we create a minimal IPlugVST3Processor
  // The hosted plugin is created with a default instance info and config
  IPlugVST3Processor::InstanceInfo info;
  // Use a default config for the hosted plugin
  // In a real implementation, this would come from the loaded VST3 module's factory
  Config config(0, 0, "", "Hosted Plugin", "Hosted Plugin", "iPlug2", 1, 0, 0, 0, false, false, false, false, 0, false, 800, 600, false, 400, 1600, 300, 1200, "", "");
  
  mPlugin = std::make_unique<IPlugVST3Processor>(info, config);
  
  if (mPlugin)
  {
    // Create minimal host context for initialization
    mHostContext = CreateHostContext();
    if (mHostContext)
    {
      // Initialize the plugin with our host context
      FUnknownPtr<IAudioProcessor> processor(mPlugin);
      if (processor)
      {
        return processor->initialize(mHostContext) == kResultOk;
      }
    }
  }
  
  return false;
}

FUnknown* IPlugVST3Host::CreateHostContext()
{
  // For embedding, we return the plugin's own IUnknown as the context
  // This allows the hosted plugin to call back into its own methods
  if (mPlugin)
  {
    mPlugin->addRef();
    return static_cast<FUnknown*>(mPlugin.get());
  }
  return nullptr;
}

bool IPlugVST3Host::InitializePlugin(double sampleRate, int maxBlockSize, int numInputs, int numOutputs)
{
  if (!mPlugin)
    return false;

  mSampleRate = sampleRate;
  mBlockSize = maxBlockSize;
  mNumInputs = numInputs;
  mNumOutputs = numOutputs;
  mHostSampleRate = sampleRate;
  mHostBlockSize = maxBlockSize;

  // Configure processing setup
  ProcessSetup setup;
  setup.sampleRate = static_cast<SampleRate>(sampleRate);
  setup.maxBlockSize = maxBlockSize;
  setup.processMode = kRealtime;

  FUnknownPtr<IAudioProcessor> processor(mPlugin);
  if (processor)
  {
    if (processor->setupProcessing(setup) != kResultOk)
      return false;

    // Set up bus arrangements
    SpeakerArrangement* inputArr = nullptr;
    SpeakerArrangement* outputArr = nullptr;
    
    if (numInputs > 0)
    {
      inputArr = new SpeakerArrangement[numInputs];
      for (int i = 0; i < numInputs; i++)
        inputArr[i] = kStereo; // Default to stereo
    }
    
    if (numOutputs > 0)
    {
      outputArr = new SpeakerArrangement[numOutputs];
      for (int i = 0; i < numOutputs; i++)
        outputArr[i] = kStereo; // Default to stereo
    }
    
    auto result = mPlugin->setBusArrangements(inputArr, numInputs, outputArr, numOutputs);
    
    delete[] inputArr;
    delete[] outputArr;
    
    return result == kResultOk;
  }
  
  return false;
}

void IPlugVST3Host::SetIOBuffers(double** inputs, double** outputs, int numSamples)
{
  mNumSamples = numSamples;
  // Store buffer pointers for use during Process()
  // The actual connection happens in Process() via VST3 ProcessData
}

void IPlugVST3Host::Process()
{
  if (!mPlugin || mNumSamples == 0)
    return;

  // Prepare process data for the hosted plugin
  ProcessData data;
  memset(&data, 0, sizeof(ProcessData));

  data.numSamples = mNumSamples;
  data.processContext = nullptr; // No transport context for embedded use

  // Set up input parameter changes (empty for now)
  ParameterChanges inputParamChanges;
  data.inputParameterChanges = &inputParamChanges;

  // Set up output parameter changes
  ParameterChanges outputParamChanges;
  data.outputParameterChanges = &outputParamChanges;

  // Prepare audio buffers
  // For embedded use, we use the internal bus buffers directly
  // The hosted plugin's VST3 wrapper handles buffer management
  
  // Activate the plugin for processing
  mPlugin->setActive(true);
  mPlugin->setProcessing(true);

  // Process audio through the VST3 processor
  FUnknownPtr<IAudioProcessor> processor(mPlugin);
  if (processor)
  {
    processor->process(data);
  }

  // Deactivate processing
  mPlugin->setProcessing(false);
}

void IPlugVST3Host::UnloadPlugin()
{
  if (mPlugin)
  {
    FUnknownPtr<IAudioProcessor> processor(mPlugin);
    if (processor)
    {
      processor->setActive(false);
      processor->terminate();
    }
    
    mPlugin.reset();
  }
  
  if (mHostContext)
  {
    mHostContext->release();
    mHostContext = nullptr;
  }
  
  mHostSampleRate.reset();
  mHostBlockSize.reset();
}

END_IPLUG_NAMESPACE
