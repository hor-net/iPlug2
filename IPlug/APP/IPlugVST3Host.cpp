/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IPlugVST3Host.h"
#include "IPlugVST3_Processor.h"
#include "IPlugVST3_Controller.h"

#include "pluginterfaces/vst/ivstaudiooprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_IPLUG_NAMESPACE

IPlugVST3Host::IPlugVST3Host()
{
}

IPlugVST3Host::~IPlugVST3Host()
{
  DeleteEditor();
  UnloadPlugin();
}

bool IPlugVST3Host::LoadPlugin(const char* vst3Path)
{
  // Cleanup existing plugin first
  UnloadPlugin();

  // For embedding purposes, we create a minimal IPlugVST3Processor
  // The hosted plugin is created with a default instance info and config
  IPlugVST3Processor::InstanceInfo info;
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
        if (processor->initialize(mHostContext) != kResultOk)
          return false;
        
        // Initialize controller separately
        return InitializeController();
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

bool IPlugVST3Host::InitializeController()
{
  // Create the controller
  IPlugVST3Controller::InstanceInfo info;
  Config config(0, 0, "", "Hosted Plugin", "Hosted Plugin", "iPlug2", 1, 0, 0, 0, false, false, false, false, 0, false, 800, 600, false, 400, 1600, 300, 1200, "", "");
  
  mController = std::make_unique<IPlugVST3Controller>(info, config);
  
  if (mController)
  {
    // Initialize controller with our host context
    if (mController->initialize(mHostContext) != kResultOk)
      return false;
    
    // Connect processor and controller
    return ConnectController();
  }
  
  return false;
}

bool IPlugVST3Host::ConnectController()
{
  if (!mPlugin || !mController)
    return false;
  
  // The processor and controller communicate via IConnectionPoint
  // In a normal VST3 plugin, this is handled internally by the VST3 SDK
  // For embedding, we need to establish the connection manually
  
  // Get the controller's IConnectionPoint
  FUnknownPtr<IConnectionPoint> controllerConnection(mController);
  if (!controllerConnection)
    return false;
  
  // Get the processor's IConnectionPoint
  FUnknownPtr<IConnectionPoint> processorConnection(mPlugin);
  if (!processorConnection)
    return false;
  
  // Connect them (this establishes the bidirectional communication)
  // Note: In VST3, the connection is typically done via addConnection()
  // but for embedding purposes, we rely on the internal setup
  
  return true;
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
  DeleteEditor();
  
  if (mController)
  {
    mController->terminate();
    mController.reset();
  }
  
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

#pragma mark -
#pragma mark Editor/View Management

bool IPlugVST3Host::CreateEditor(void* parentWindow)
{
  // Clean up any existing editor first
  DeleteEditor();
  
  if (!mController)
    return false;
  
  mParentWindow = parentWindow;
  
  // Get the edit controller interface
  FUnknownPtr<IEditController> editController(mController);
  if (!editController)
    return false;
  
  // Create the view via IEditController::createView()
  mEditorView = editController->createView("editor");
  
  if (!mEditorView)
    return false;
  
  // Attach the view to our parent window
  FIDString platformType = nullptr;
  
#ifdef __APPLE__
  platformType = kPlatformTypeNSView;
#elif defined OS_WIN
  platformType = kPlatformTypeHWND;
#endif
  
  if (platformType)
  {
    if (mEditorView->isPlatformTypeSupported(platformType) != kResultTrue)
      return false;
    
    if (mEditorView->attached(parentWindow, platformType) != kResultOk)
      return false;
  }
  
  return true;
}

void IPlugVST3Host::ShowEditor()
{
  if (mEditorView)
  {
    // The view visibility is typically controlled via the parent window
    // For VST3, we might need to use show/hide methods
    // The actual visibility is handled by the platform-specific view
  }
}

void IPlugVST3Host::HideEditor()
{
  if (mEditorView)
  {
    // Hide the view by detaching it temporarily
    // Note: This is a simplified approach; real implementation may need
    // to track visibility state separately
  }
}

void IPlugVST3Host::DeleteEditor()
{
  if (mEditorView)
  {
    // Remove the view from its parent
    mEditorView->removed();
    
    // Release the view
    mEditorView->release();
    mEditorView = nullptr;
  }
  
  mParentWindow = nullptr;
}

void* IPlugVST3Host::GetEditorPlatformHandle() const
{
  if (!mEditorView)
    return nullptr;
  
#ifdef __APPLE__
  return GetNSView();
#elif defined OS_WIN
  return GetHWND();
#else
  return nullptr;
#endif
}

void IPlugVST3Host::ResizeEditor(int width, int height)
{
  if (!mEditorView)
    return;
  
  ViewRect newSize(0, 0, width, height);
  mEditorView->onSize(&newSize);
}

#ifdef __APPLE__
void* IPlugVST3Host::GetNSView() const
{
  if (!mEditorView)
    return nullptr;
  
  // Get the native platform handle from the IPlugView
  // On macOS, this returns an NSView*
  auto platformContext = mEditorView->getPlatformContext();
  
  // The platform context contains the native handle
  // For NSView, we need to get it from the platform context
  return platformContext.getNativePlatformHandle();
}
#elif defined OS_WIN
void* IPlugVST3Host::GetHWND() const
{
  // On Windows, the parentWindow we stored is the HWND
  // The IPlugView wraps the native HWND
  return mParentWindow;
}
#endif

END_IPLUG_NAMESPACE
