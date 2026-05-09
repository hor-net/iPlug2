/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

/**
 * @file IPlugVST3Host.h
 * @brief VST3 Plugin Host for embedding VST3 plugins inside IPlug
 */

#pragma once

#include <memory>
#include <string>
#include <optional>

#include "IPlugPlatform.h"
#include "IPlugVST3.h"
#include "IPlugVST3_Processor.h"
#include "IPlugVST3_ProcessorBase.h"

BEGIN_IPLUG_NAMESPACE

// Forward declarations
class IPlugVST3Processor;
class IPlugVST3Controller;

/** A class that hosts VST3 plugins, usable in standalone IPlugAPP or embedded in another VST3 plugin */
class IPlugVST3Host
{
public:
  /** Construct the VST3 host */
  IPlugVST3Host();

  /** Destructor - cleans up hosted plugin if still loaded */
  ~IPlugVST3Host();

  // Non-copyable
  IPlugVST3Host(const IPlugVST3Host&) = delete;
  IPlugVST3Host& operator=(const IPlugVST3Host&) = delete;

  // Movable
  IPlugVST3Host(IPlugVST3Host&&) = default;
  IPlugVST3Host& operator=(IPlugVST3Host&&) = default;

  /**
   * Load a VST3 plugin from a .vst3 bundle path.
   * @param vst3Path Path to the VST3 plugin bundle/directory
   * @return true if plugin loaded successfully
   */
  bool LoadPlugin(const char* vst3Path);

  /**
   * Get the hosted VST3 plugin instance.
   * @return Pointer to the hosted IPlugVST3Processor, or nullptr if not loaded
   */
  IPlugVST3Processor* GetPlugin() const { return mPlugin.get(); }

  /**
   * Check if a plugin is currently loaded.
   * @return true if a plugin is loaded
   */
  bool HasPlugin() const { return mPlugin != nullptr; }

  /**
   * Initialize the hosted plugin with audio settings.
   * @param sampleRate Sample rate for processing
   * @param maxBlockSize Maximum block size in samples
   * @param numInputs Number of input buses
   * @param numOutputs Number of output buses
   * @return true if initialization succeeded
   */
  bool InitializePlugin(double sampleRate, int maxBlockSize, int numInputs, int numOutputs);

  /**
   * Connect audio buffers for processing.
   * @param inputs Array of input buffer pointers (or nullptr for none)
   * @param outputs Array of output buffer pointers (or nullptr for none)
   * @param numSamples Number of samples to process
   */
  void SetIOBuffers(double** inputs, double** outputs, int numSamples);

  /**
   * Process audio with the hosted plugin.
   * Call SetIOBuffers() before this for each process call.
   */
  void Process();

  /**
   * Cleanup and unload the hosted plugin.
   */
  void UnloadPlugin();

  /**
   * Get the current sample rate.
   * @return Sample rate, or 0 if not initialized
   */
  double GetSampleRate() const { return mSampleRate; }

  /**
   * Get the current block size.
   * @return Block size, or 0 if not initialized
   */
  int GetBlockSize() const { return mBlockSize; }

  /**
   * Get the number of input channels.
   * @return Number of inputs, or -1 if no plugin
   */
  int GetNumInputs() const { return mNumInputs; }

  /**
   * Get the number of output channels.
   * @return Number of outputs, or -1 if no plugin
   */
  int GetNumOutputs() const { return mNumOutputs; }

  //----------------------------------------------------------------------------
  // Editor/View Management

  /**
   * Create the editor view for the hosted plugin.
   * @param parentWindow Platform-specific parent window handle (HWND on Windows, NSView* on macOS)
   * @return true if editor created successfully
   */
  bool CreateEditor(void* parentWindow);

  /**
   * Show the editor window.
   */
  void ShowEditor();

  /**
   * Hide the editor window.
   */
  void HideEditor();

  /**
   * Destroy the editor view.
   */
  void DeleteEditor();

  /**
   * Check if the editor exists.
   * @return true if editor is created
   */
  bool HasEditor() const { return mEditorView != nullptr; }

  /**
   * Get the native platform handle for the editor view.
   * @return HWND on Windows, NSView* on macOS, nullptr if no editor
   */
  void* GetEditorPlatformHandle() const;

  /**
   * Resize the editor view.
   * @param width New width in pixels
   * @param height New height in pixels
   */
  void ResizeEditor(int width, int height);

  /**
   * Get the controller for the hosted plugin.
   * @return Pointer to IPlugVST3Controller, or nullptr if not loaded
   */
  IPlugVST3Controller* GetController() const { return mController.get(); }

  //----------------------------------------------------------------------------
  // Platform-specific accessors

#ifdef __APPLE__
  /**
   * Get the NSView pointer for the editor (macOS only).
   * @return NSView* pointer, or nullptr if no editor
   */
  void* GetNSView() const;
#elif defined OS_WIN
  /**
   * Get the HWND for the editor (Windows only).
   * @return HWND handle, or nullptr if no editor
   */
  void* GetHWND() const;
#endif

private:
  /** Helper to create minimal host context for the plugin */
  Steinberg::FUnknown* CreateHostContext();

  /** Helper to initialize the controller */
  bool InitializeController();

  /** Helper to connect processor and controller via IConnectionPoint */
  bool ConnectController();

  std::unique_ptr<IPlugVST3Processor> mPlugin = nullptr;
  std::unique_ptr<IPlugVST3Controller> mController = nullptr;

  // Editor view owned by the controller
  Steinberg::IPlugView* mEditorView = nullptr;

  // Platform-specific parent window handle
  void* mParentWindow = nullptr;

  double mSampleRate = 44100.0;
  int mBlockSize = 512;
  int mNumInputs = 0;
  int mNumOutputs = 0;
  int mNumSamples = 0;

  std::optional<double> mHostSampleRate;
  std::optional<int> mHostBlockSize;

  // Owned host context - released when plugin is unloaded
  Steinberg::FUnknown* mHostContext = nullptr;
};

END_IPLUG_NAMESPACE
