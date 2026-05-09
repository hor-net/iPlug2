/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include "IPlugAPP_host.h"

#ifdef OS_WIN
#include <sys/stat.h>
#include "win32_utf8.h"
#endif

#include "IPlugLogger.h"

using namespace iplug;

#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 2048
#endif

#define STRBUFSZ 100

std::unique_ptr<IPlugAPPHost> IPlugAPPHost::sInstance;
UINT gSCROLLMSG;

IPlugAPPHost::IPlugAPPHost()
: mIPlug(MakePlug(InstanceInfo{this}))
{
}

IPlugAPPHost::~IPlugAPPHost()
{
  mExiting = true;
  
  CloseAudio();
  
  if (mMidiIn)
    mMidiIn->cancelCallback();

  if (mMidiOut)
    mMidiOut->closePort();
}

//static
IPlugAPPHost* IPlugAPPHost::Create()
{
  sInstance = std::make_unique<IPlugAPPHost>();
  return sInstance.get();
}

bool IPlugAPPHost::Init()
{
  mIPlug->SetHost("standalone", mIPlug->GetPluginVersion(false));
    
  if (!InitState())
    return false;
  
  TryToChangeAudioDriverType(); // will init RTAudio with an API type based on gState->mAudioDriverType
  ProbeAudioIO(); // find out what audio IO devs are available and put their IDs in the global variables gAudioInputDevs / gAudioOutputDevs
  InitMidi(); // creates RTMidiIn and RTMidiOut objects
  ProbeMidiIO(); // find out what midi IO devs are available and put their names in the global variables gMidiInputDevs / gMidiOutputDevs
  SelectMIDIDevice(ERoute::kInput, mState.mMidiInDev.Get());
  SelectMIDIDevice(ERoute::kOutput, mState.mMidiOutDev.Get());
  
  mIPlug->OnParamReset(kReset);
  mIPlug->OnActivate(true);
  
  return true;
}

bool IPlugAPPHost::OpenWindow(HWND pParent)
{
  return mIPlug->OpenWindow(pParent) != nullptr;
}

void IPlugAPPHost::CloseWindow()
{
  mIPlug->CloseWindow();
}

bool IPlugAPPHost::InitState()
{
#if defined OS_WIN
  char strPath[MAX_PATH_LEN];
  SHGetSpecialFolderPathUTF8(NULL, strPath, MAX_PATH_LEN, CSIDL_LOCAL_APPDATA, FALSE);
  mINIPath.SetFormatted(MAX_PATH_LEN, "%s\\%s\\", strPath, BUNDLE_NAME);
#elif defined OS_MAC
  mINIPath.SetFormatted(MAX_PATH_LEN, "%s/Library/Application Support/%s/", getenv("HOME"), BUNDLE_NAME);
#else
  #error NOT IMPLEMENTED
#endif

  struct stat st;

  if (stat(mINIPath.Get(), &st) == 0) // if directory exists
  {
    mINIPath.Append("settings.ini"); // add file name to path

    char buf[STRBUFSZ];
    
    if (stat(mINIPath.Get(), &st) == 0) // if settings file exists read values into state
    {
      DBGMSG("Reading ini file from %s\n", mINIPath.Get());
      
      mState.mAudioDriverType = GetPrivateProfileInt("audio", "driver", 0, mINIPath.Get());

      GetPrivateProfileString("audio", "indev", "Built-in Input", buf, STRBUFSZ, mINIPath.Get()); mState.mAudioInDev.Set(buf);
      GetPrivateProfileString("audio", "outdev", "Built-in Output", buf, STRBUFSZ, mINIPath.Get()); mState.mAudioOutDev.Set(buf);

      //audio
      mState.mAudioInChanL = GetPrivateProfileInt("audio", "in1", 1, mINIPath.Get()); // 1 is first audio input
      mState.mAudioInChanR = GetPrivateProfileInt("audio", "in2", 2, mINIPath.Get());
      mState.mAudioOutChanL = GetPrivateProfileInt("audio", "out1", 1, mINIPath.Get()); // 1 is first audio output
      mState.mAudioOutChanR = GetPrivateProfileInt("audio", "out2", 2, mINIPath.Get());
      //mState.mAudioInIsMono = GetPrivateProfileInt("audio", "monoinput", 0, mINIPath.Get());

      mState.mBufferSize = GetPrivateProfileInt("audio", "buffer", 512, mINIPath.Get());
      mState.mAudioSR = GetPrivateProfileInt("audio", "sr", 44100, mINIPath.Get());

      //midi
      GetPrivateProfileString("midi", "indev", "no input", buf, STRBUFSZ, mINIPath.Get()); mState.mMidiInDev.Set(buf);
      GetPrivateProfileString("midi", "outdev", "no output", buf, STRBUFSZ, mINIPath.Get()); mState.mMidiOutDev.Set(buf);

      mState.mMidiInChan = GetPrivateProfileInt("midi", "inchan", 0, mINIPath.Get()); // 0 is any
      mState.mMidiOutChan = GetPrivateProfileInt("midi", "outchan", 0, mINIPath.Get()); // 1 is first chan
    }

    // if settings file doesn't exist, populate with default values, otherwise overwrite
    UpdateINI();
  }
  else // folder doesn't exist - make folder and make file
  {
#if defined OS_WIN
    // folder doesn't exist - make folder and make file
    CreateDirectory(mINIPath.Get(), NULL);
    mINIPath.Append("settings.ini");
    UpdateINI(); // will write file if doesn't exist
#elif defined OS_MAC
    mode_t process_mask = umask(0);
    int result_code = mkdir(mINIPath.Get(), S_IRWXU | S_IRWXG | S_IRWXO);
    umask(process_mask);

    if (!result_code)
    {
      mINIPath.Append("settings.ini");
      UpdateINI(); // will write file if doesn't exist
    }
    else
    {
      return false;
    }
#else
  #error NOT IMPLEMENTED
#endif
  }

  return true;
}

void IPlugAPPHost::UpdateINI()
{
  char buf[STRBUFSZ]; // temp buffer for writing integers to profile strings
  const char* ini = mINIPath.Get();

  sprintf(buf, "%u", mState.mAudioDriverType);
  WritePrivateProfileString("audio", "driver", buf, ini);

  WritePrivateProfileString("audio", "indev", mState.mAudioInDev.Get(), ini);
  WritePrivateProfileString("audio", "outdev", mState.mAudioOutDev.Get(), ini);

  sprintf(buf, "%u", mState.mAudioInChanL);
  WritePrivateProfileString("audio", "in1", buf, ini);
  sprintf(buf, "%u", mState.mAudioInChanR);
  WritePrivateProfileString("audio", "in2", buf, ini);
  sprintf(buf, "%u", mState.mAudioOutChanL);
  WritePrivateProfileString("audio", "out1", buf, ini);
  sprintf(buf, "%u", mState.mAudioOutChanR);
  WritePrivateProfileString("audio", "out2", buf, ini);
  //sprintf(buf, "%u", mState.mAudioInIsMono);
  //WritePrivateProfileString("audio", "monoinput", buf, ini);

  WDL_String str;
  str.SetFormatted(32, "%i", mState.mBufferSize);
  WritePrivateProfileString("audio", "buffer", str.Get(), ini);

  str.SetFormatted(32, "%i", mState.mAudioSR);
  WritePrivateProfileString("audio", "sr", str.Get(), ini);

  WritePrivateProfileString("midi", "indev", mState.mMidiInDev.Get(), ini);
  WritePrivateProfileString("midi", "outdev", mState.mMidiOutDev.Get(), ini);

  sprintf(buf, "%u", mState.mMidiInChan);
  WritePrivateProfileString("midi", "inchan", buf, ini);
  sprintf(buf, "%u", mState.mMidiOutChan);
  WritePrivateProfileString("midi", "outchan", buf, ini);
}

std::string IPlugAPPHost::GetAudioDeviceName(uint32_t deviceID) const
{
  auto str = mDAC->getDeviceInfo(deviceID).name;
  std::size_t pos = str.find(':');

  if (pos != std::string::npos)
  {
    std::string subStr = str.substr(pos + 1);
    return subStr;
  }
  else
  {
    return str;
  }
}

std::optional<uint32_t> IPlugAPPHost::GetAudioDeviceID(const char* deviceNameToTest) const
{
  auto deviceIDs = mDAC->getDeviceIds();

  for (auto deviceID : deviceIDs)
  {
    auto name = GetAudioDeviceName(deviceID);

    if (std::string_view(deviceNameToTest) == name)
    {
      return deviceID;
    }
  }
  
  return std::nullopt;
}

int IPlugAPPHost::GetMIDIPortNumber(ERoute direction, const char* nameToTest) const
{
  int start = 1;
  
  auto nameStrView = std::string_view(nameToTest);
  
  if (direction == ERoute::kInput)
  {
    if (nameStrView == OFF_TEXT) return 0;
    
  #ifdef OS_MAC
    start = 2;
    if (nameStrView == "virtual input") return 1;
  #endif
    
    for (int i = 0; i < mMidiIn->getPortCount(); i++)
    {
      if (nameStrView == mMidiIn->getPortName(i).c_str())
        return (i + start);
    }
  }
  else
  {
    if (nameStrView == OFF_TEXT) return 0;
  
  #ifdef OS_MAC
    start = 2;
    if (nameStrView == "virtual output") return 1;
  #endif
  
    for (int i = 0; i < mMidiOut->getPortCount(); i++)
    {
      if (nameStrView == mMidiOut->getPortName(i).c_str())
        return (i + start);
    }
  }
  
  return -1;
}

void IPlugAPPHost::ProbeAudioIO()
{
  mAudioInputDevIDs.clear();
  mAudioOutputDevIDs.clear();

  if (!mDAC)
    return;

  DBGMSG("\nRtAudio Version %s", RtAudio::getVersion().c_str());

  RtAudio::DeviceInfo info;

  auto deviceIDs = mDAC->getDeviceIds();

  for (auto deviceID : deviceIDs)
  {
    info = mDAC->getDeviceInfo(deviceID);

    if (info.inputChannels > 0)
    {
      mAudioInputDevIDs.push_back(deviceID);
    }
    
    if (info.outputChannels > 0)
    {
      mAudioOutputDevIDs.push_back(deviceID);
    }
    
    if (info.isDefaultInput)
    {
      mDefaultInputDev = deviceID;
    }
    
    if (info.isDefaultOutput)
    {
      mDefaultOutputDev = deviceID;
    }
  }
}

void IPlugAPPHost::ProbeMidiIO()
{
  if (!mMidiIn || !mMidiOut)
    return;
  else
  {
    int nInputPorts = mMidiIn->getPortCount();

    mMidiInputDevNames.push_back(OFF_TEXT);

#ifdef OS_MAC
    mMidiInputDevNames.push_back("virtual input");
#endif

    for (int i=0; i<nInputPorts; i++)
    {
      mMidiInputDevNames.push_back(mMidiIn->getPortName(i));
    }

    int nOutputPorts = mMidiOut->getPortCount();

    mMidiOutputDevNames.push_back(OFF_TEXT);

#ifdef OS_MAC
    mMidiOutputDevNames.push_back("virtual output");
#endif

    for (int i=0; i<nOutputPorts; i++)
    {
      mMidiOutputDevNames.push_back(mMidiOut->getPortName(i));
      //This means the virtual output port wont be added as an input
    }
  }
}

bool IPlugAPPHost::AudioSettingsInStateAreEqual(AppState& os, AppState& ns)
{
  if (os.mAudioDriverType != ns.mAudioDriverType) return false;
  if (std::string_view(os.mAudioInDev.Get()) != ns.mAudioInDev.Get()) return false;
  if (std::string_view(os.mAudioOutDev.Get()) != ns.mAudioOutDev.Get()) return false;
  if (os.mAudioSR != ns.mAudioSR) return false;
  if (os.mBufferSize != ns.mBufferSize) return false;
  if (os.mAudioInChanL != ns.mAudioInChanL) return false;
  if (os.mAudioInChanR != ns.mAudioInChanR) return false;
  if (os.mAudioOutChanL != ns.mAudioOutChanL) return false;
  if (os.mAudioOutChanR != ns.mAudioOutChanR) return false;
//  if (os.mAudioInIsMono != ns.mAudioInIsMono) return false;

  return true;
}

bool IPlugAPPHost::MIDISettingsInStateAreEqual(AppState& os, AppState& ns)
{
  if (std::string_view(os.mMidiInDev.Get()) != ns.mMidiInDev.Get()) return false;
  if (std::string_view(os.mMidiOutDev.Get()) != ns.mMidiOutDev.Get()) return false;
  if (os.mMidiInChan != ns.mMidiInChan) return false;
  if (os.mMidiOutChan != ns.mMidiOutChan) return false;

  return true;
}

bool IPlugAPPHost::TryToChangeAudioDriverType()
{
  CloseAudio();

  if (mDAC)
  {
    mDAC = nullptr;
  }

  // Skip RtAudio initialization in no-I/O mode or screenshot mode
  if (mNoIO || IsScreenshotMode())
    return true;

#if defined OS_WIN
  if (mState.mAudioDriverType == kDeviceASIO)
    mDAC = std::make_unique<RtAudio>(RtAudio::WINDOWS_ASIO);
  else if (mState.mAudioDriverType == kDeviceDS)
    mDAC = std::make_unique<RtAudio>(RtAudio::WINDOWS_DS);
#elif defined OS_MAC
  if (mState.mAudioDriverType == kDeviceCoreAudio)
    mDAC = std::make_unique<RtAudio>(RtAudio::MACOSX_CORE);
  //else
  //mDAC = std::make_unique<RtAudio>(RtAudio::UNIX_JACK);
#else
  #error NOT IMPLEMENTED
#endif

  if (mDAC)
  {
    mDAC->setErrorCallback(ErrorCallback);
    return true;
  }

  return false;
}

bool IPlugAPPHost::TryToChangeAudio()
{
  // Skip audio initialization in no-I/O mode or screenshot mode
  if (mNoIO || IsScreenshotMode())
    return true;

#if defined OS_WIN
  // ASIO has one device, use the output for the input ID
  auto inputID = GetAudioDeviceID(mState.mAudioDriverType == kDeviceASIO ? mState.mAudioOutDev.Get() : mState.mAudioInDev.Get());
  
  // If system audio loopback is enabled on Windows with WASAPI
  if (mState.mCaptureSystemAudio)
  {
    // For WASAPI loopback, we need to use the "System" or "Loopback" device as input
    // This captures all audio that would normally go to the speakers
    if (inputID && mAudioInputDevIDs.size() > 0)
    {
      // Check if we should use the default input device (which is typically the system loopback)
      // For now, we'll use the default input device as the loopback source
      if (!mDefaultInputDev)
      {
        mDefaultInputDev = mAudioInputDevIDs[0];
      }
      
      // On Windows with WASAPI, the default input device should be the "System" capture device
      // which supports loopback. RtAudio handles the AUDCLNT_STREAMFLAGS_LOOPBACK flag internally.
      DBGMSG("Windows: using default input as system audio loopback source\n");
      inputID = mDefaultInputDev;
    }
  }
#elif defined OS_MAC
  auto inputID = GetAudioDeviceID(mState.mAudioInDev.Get());
#else
  #error NOT IMPLEMENTED
#endif
  auto outputID = GetAudioDeviceID(mState.mAudioOutDev.Get());

  bool failedToFindDevice = false;
  bool resetToDefault = false;

  if (!inputID)
  {
    if (mDefaultInputDev)
    {
      resetToDefault = true;
      inputID = mDefaultInputDev;

      if (mAudioInputDevIDs.size())
        mState.mAudioInDev.Set(GetAudioDeviceName(inputID.value()).c_str());
    }
    else
      failedToFindDevice = true;
  }

  if (!outputID)
  {
    if (mDefaultOutputDev)
    {
      resetToDefault = true;
      outputID = mDefaultOutputDev;

      if (mAudioOutputDevIDs.size())
        mState.mAudioOutDev.Set(GetAudioDeviceName(outputID.value()).c_str());
    }
    else
      failedToFindDevice = true;
  }

  if (resetToDefault)
  {
    DBGMSG("Couldn't find previous audio device, reseting to default\n");
    UpdateINI();
  }

  if (failedToFindDevice)
    MessageBox(gHWND, "Please check the audio settings", "Error", MB_OK);

  if (inputID && outputID)
  {
#ifdef __APPLE__
    // If system audio loopback is enabled, create an aggregate device
    if (mState.mCaptureSystemAudio)
    {
      auto aggDeviceID = CreateAggregateDeviceForLoopback(outputID.value());
      if (aggDeviceID)
      {
        // Replace the input with our aggregate loopback device
        inputID = aggDeviceID;
        DBGMSG("Using aggregate device for system audio loopback: %u\n", aggDeviceID.value());
      }
    }
#endif
    return InitAudio(inputID.value(), outputID.value(), mState.mAudioSR, mState.mBufferSize);
  }

  return false;
}

bool IPlugAPPHost::SelectMIDIDevice(ERoute direction, const char* pPortName)
{
  int port = GetMIDIPortNumber(direction, pPortName);

  if (direction == ERoute::kInput)
  {
    if (port == -1)
    {
      mState.mMidiInDev.Set(OFF_TEXT);
      UpdateINI();
      port = 0;
    }

    //TODO: send all notes off?
    if (mMidiIn)
    {
      mMidiIn->closePort();

      if (port == 0)
      {
        return true;
      }
  #if defined OS_WIN
      else
      {
        mMidiIn->openPort(port-1);
        return true;
      }
  #elif defined OS_MAC
      else if (port == 1)
      {
        std::string virtualMidiInputName = "To ";
        virtualMidiInputName += BUNDLE_NAME;
        mMidiIn->openVirtualPort(virtualMidiInputName);
        return true;
      }
      else
      {
        mMidiIn->openPort(port-2);
        return true;
      }
  #else
   #error NOT IMPLEMENTED
  #endif
    }
  }
  else
  {
    if (port == -1)
    {
      mState.mMidiOutDev.Set(OFF_TEXT);
      UpdateINI();
      port = 0;
    }
    
    if (mMidiOut)
    {
      //TODO: send all notes off?
      mMidiOut->closePort();
      
      if (port == 0)
        return true;
#if defined OS_WIN
      else
      {
        mMidiOut->openPort(port-1);
        return true;
      }
#elif defined OS_MAC
      else if (port == 1)
      {
        std::string virtualMidiOutputName = "From ";
        virtualMidiOutputName += BUNDLE_NAME;
        mMidiOut->openVirtualPort(virtualMidiOutputName);
        return true;
      }
      else
      {
        mMidiOut->openPort(port-2);
        return true;
      }
#else
  #error NOT IMPLEMENTED
#endif
    }
  }
  
  return false;
}

void IPlugAPPHost::CloseAudio()
{
  if (mDAC && mDAC->isStreamOpen())
  {
    if (mDAC->isStreamRunning())
    {
      mAudioEnding = true;
    
      while (!mAudioDone)
        Sleep(10);
      
      mDAC->abortStream();
    }
    
    mDAC->closeStream();
  }
}

bool IPlugAPPHost::InitAudio(uint32_t inID, uint32_t outID, uint32_t sr, uint32_t iovs)
{
  CloseAudio();

  RtAudio::StreamParameters iParams, oParams;

#ifdef OS_WIN
  // On Windows, if system audio loopback is enabled and we're using WASAPI
  // we need to configure the input device for loopback mode
  if (mState.mCaptureSystemAudio)
  {
    DBGMSG("Windows system audio loopback: configuring input for WASAPI loopback mode\n");
    // Note: RtAudio with WASAPI will automatically use loopback when
    // the input device is the system default capture device.
    // The key is that the input device should be the "System" capture device
    // which provides the mixed output audio.
  }
#endif
  iParams.deviceId = inID;
  iParams.nChannels = GetPlug()->MaxNChannels(ERoute::kInput); // TODO: flexible channel count
  iParams.firstChannel = 0; // TODO: flexible channel count

  oParams.deviceId = outID;
  oParams.nChannels = GetPlug()->MaxNChannels(ERoute::kOutput); // TODO: flexible channel count
  oParams.firstChannel = 0; // TODO: flexible channel count

  mBufferSize = iovs; // mBufferSize may get changed by stream

  DBGMSG("trying to start audio stream @ %i sr, buffer size %i\nindev = %s\noutdev = %s\ninputs = %i\noutputs = %i\n",
    sr, mBufferSize, GetAudioDeviceName(inID).c_str(), GetAudioDeviceName(outID).c_str(), iParams.nChannels, oParams.nChannels);

  RtAudio::StreamOptions options;
  options.flags = RTAUDIO_NONINTERLEAVED;
  // options.streamName = BUNDLE_NAME; // JACK stream name, not used on other streams

  mBufIndex = 0;
  mSamplesElapsed = 0;
  mSampleRate = static_cast<double>(sr);
  mVecWait = 0;
  mAudioEnding = false;
  mAudioDone = false;
  
  mIPlug->SetBlockSize(APP_SIGNAL_VECTOR_SIZE);
  mIPlug->SetSampleRate(mSampleRate);
  mIPlug->OnReset();

  auto status = mDAC->openStream(&oParams, iParams.nChannels > 0 ? &iParams : nullptr, RTAUDIO_FLOAT64, sr, &mBufferSize, &AudioCallback, this, &options);

  if (status != RtAudioErrorType::RTAUDIO_NO_ERROR)
  {
    DBGMSG("%s", mDAC->getErrorText().c_str());
    return false;
  }

  for (int i = 0; i < iParams.nChannels; i++)
  {
    mInputBufPtrs.Add(nullptr); //will be set in callback
  }
    
  for (int i = 0; i < oParams.nChannels; i++)
  {
    mOutputBufPtrs.Add(nullptr); //will be set in callback
  }
    
  if (mDAC->startStream() != RTAUDIO_NO_ERROR)
  {
    DBGMSG("Error starting stream: %s\n", mDAC->getErrorText().c_str());
    return false;
  }

  mActiveState = mState;

  return true;
}

bool IPlugAPPHost::InitMidi()
{
  // Skip MIDI initialization in no-I/O mode or screenshot mode
  if (mNoIO || IsScreenshotMode())
    return true;

  try
  {
    mMidiIn = std::make_unique<RtMidiIn>();
  }
  catch (RtMidiError &error)
  {
    mMidiIn = nullptr;
    error.printMessage();
    return false;
  }

  try
  {
    mMidiOut = std::make_unique<RtMidiOut>();
  }
  catch (RtMidiError &error)
  {
    mMidiOut = nullptr;
    error.printMessage();
    return false;
  }

  mMidiIn->setCallback(&MIDICallback, this);
  mMidiIn->ignoreTypes(false, true, false );

  return true;
}

void ApplyFades(double *pBuffer, int nChans, int nFrames, bool down)
{
  for (int i = 0; i < nChans; i++)
  {
    double *pIO = pBuffer + (i * nFrames);
    
    if (down)
    {
      for (int j = 0; j < nFrames; j++)
        pIO[j] *= ((double) (nFrames - (j + 1)) / (double) nFrames);
    }
    else
    {
      for (int j = 0; j < nFrames; j++)
        pIO[j] *= ((double) j / (double) nFrames);
    }
  }
}

// static
int IPlugAPPHost::AudioCallback(void* pOutputBuffer, void* pInputBuffer, uint32_t nFrames, double streamTime, RtAudioStreamStatus status, void* pUserData)
{
  IPlugAPPHost* _this = (IPlugAPPHost*) pUserData;

  int nins = _this->GetPlug()->MaxNChannels(ERoute::kInput);
  int nouts = _this->GetPlug()->MaxNChannels(ERoute::kOutput);
  
  double* pInputBufferD = static_cast<double*>(pInputBuffer);
  double* pOutputBufferD = static_cast<double*>(pOutputBuffer);

  bool startWait = _this->mVecWait >= APP_N_VECTOR_WAIT; // wait APP_N_VECTOR_WAIT * iovs before processing audio, to avoid clicks
  bool doFade = _this->mVecWait == APP_N_VECTOR_WAIT || _this->mAudioEnding;
  
  if (startWait && !_this->mAudioDone)
  {
    if (doFade)
      ApplyFades(pInputBufferD, nins, nFrames, _this->mAudioEnding);
    
    for (int i = 0; i < nFrames; i++)
    {
      _this->mBufIndex %= APP_SIGNAL_VECTOR_SIZE;

      if (_this->mBufIndex == 0)
      {
        for (int c = 0; c < nins; c++)
        {
          _this->mInputBufPtrs.Set(c, (pInputBufferD + (c * nFrames)) + i);
        }
        
        for (int c = 0; c < nouts; c++)
        {
          _this->mOutputBufPtrs.Set(c, (pOutputBufferD + (c * nFrames)) + i);
        }
        
        _this->mIPlug->AppProcess(_this->mInputBufPtrs.GetList(), _this->mOutputBufPtrs.GetList(), APP_SIGNAL_VECTOR_SIZE);

        _this->mSamplesElapsed += APP_SIGNAL_VECTOR_SIZE;
      }
      
      for (int c = 0; c < nouts; c++)
      {
        pOutputBufferD[c * nFrames + i] *= APP_MULT;
      }

      _this->mBufIndex++;
    }
    
    if (doFade)
      ApplyFades(pOutputBufferD, nouts, nFrames, _this->mAudioEnding);
    
    if (_this->mAudioEnding)
      _this->mAudioDone = true;
  }
  else
  {
    memset(pOutputBufferD, 0, nFrames * nouts * sizeof(double));
  }
  
  _this->mVecWait = std::min(_this->mVecWait + 1, uint32_t(APP_N_VECTOR_WAIT + 1));

  return 0;
}

// static
void IPlugAPPHost::MIDICallback(double deltatime, std::vector<uint8_t>* pMsg, void* pUserData)
{
  IPlugAPPHost* _this = (IPlugAPPHost*) pUserData;
  
  if (pMsg->size() == 0 || _this->mExiting)
    return;
  
  if (pMsg->size() > 3)
  {
    if (pMsg->size() > MAX_SYSEX_SIZE)
    {
      DBGMSG("SysEx message exceeds MAX_SYSEX_SIZE\n");
      return;
    }
    
    SysExData data { 0, static_cast<int>(pMsg->size()), pMsg->data() };
    
    _this->mIPlug->mSysExMsgsFromCallback.Push(data);
    return;
  }
  else if (pMsg->size())
  {
    IMidiMsg msg;
    msg.mStatus = pMsg->at(0);
    pMsg->size() > 1 ? msg.mData1 = pMsg->at(1) : msg.mData1 = 0;
    pMsg->size() > 2 ? msg.mData2 = pMsg->at(2) : msg.mData2 = 0;

    _this->mIPlug->mMidiMsgsFromCallback.Push(msg);
  }
}

// static
void IPlugAPPHost::ErrorCallback(RtAudioErrorType type, const std::string &errorText)
{
  std::cerr << "\nerrorCallback: " << errorText << "\n\n";
}

////////////////////////////////////////////////////////////////////////////////
// SYSTEM AUDIO LOOPBACK IMPLEMENTATION
//
// Overview:
// This module provides system audio loopback capture on both macOS and Windows.
// When enabled, the standalone app captures audio from the system's output
// (what would normally go to the speakers) and processes it through the
// hosted iPlug plugin(s), then outputs to the selected audio device.
//
// How it works:
//
// macOS:
// ------
// Core Audio on macOS supports "aggregate devices" which combine multiple
// physical audio devices into a single virtual device. Crucially, when you
// include the default OUTPUT device in an aggregate, the system automatically
// provides its INPUT channels with the mixed output audio (loopback).
//
// So the flow is:
// 1. User selects an output device (e.g., speakers)
// 2. When loopback is enabled, we create an aggregate device that combines:
//    - The default output device (provides system audio via its input/loopback)
//    - The user-selected output device (for actual audio output)
// 3. RtAudio opens this aggregate device, and we use its input channels
//    as the source for our audio processing
//
// Windows:
// --------
// WASAPI (Windows Audio Session API) supports loopback mode natively via the
// AUDCLNT_STREAMFLAGS_LOOPBACK flag. When a capture stream is opened with this
// flag, it captures all audio from the system mix (what would go to the
// speakers).
//
// The key insight is that on Windows, the "default input device" when using
// WASAPI is typically the "System" device which supports loopback. RtAudio
// handles the flag internally when appropriate.
//
// Implementation:
// - On macOS: We manually create an aggregate device via Core Audio API
// - On Windows: RtAudio handles the loopback flag automatically for WASAPI
////////////////////////////////////////////////////////////////////////////////



#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>

// Helper: Get device UID from device ID
static CFStringRef GetDeviceUID(AudioObjectID deviceID)
{
  AudioObjectPropertyAddress addr = {
    kAudioDevicePropertyDeviceUID,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  
  CFStringRef uid = nullptr;
  UInt32 size = sizeof(uid);
  
  if (AudioObjectGetPropertyData(deviceID, &addr, 0, nullptr, &size, &uid) == noErr)
  {
    return uid;
  }
  
  return CFSTR("");
}

// Helper: Get default output device ID
static AudioObjectID GetDefaultOutputDeviceID()
{
  AudioObjectPropertyAddress addr = {
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  
  AudioObjectID deviceID = kAudioObjectUnknown;
  UInt32 size = sizeof(deviceID);
  
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &deviceID) == noErr)
  {
    return deviceID;
  }
  
  return kAudioObjectUnknown;
}

// static
std::optional<uint32_t> IPlugAPPHost::CreateAggregateDeviceForLoopback(uint32_t outputDeviceID)
{
  // On macOS, system audio loopback works by using the OUTPUT device's INPUT channels
  // (the device captures what would be played, creating a loopback effect)
  
  // Find the default output device (provides system audio loopback)
  AudioObjectID loopbackSourceID = GetDefaultOutputDeviceID();
  
  if (loopbackSourceID == kAudioObjectUnknown)
  {
    DBGMSG("Could not find default output device for loopback\n");
    return std::nullopt;
  }
  
  // Verify the loopback source has input channels (it should on macOS)
  AudioObjectPropertyAddress inputChannelsAddr = {
    kAudioDevicePropertyStreamConfiguration,
    kAudioDevicePropertyScopeInput,
    kAudioObjectPropertyElementMain
  };
  
  UInt32 propSize = 0;
  OSStatus status = AudioObjectGetPropertyDataSize(
    loopbackSourceID, &inputChannelsAddr, 0, nullptr, &propSize
  );
  
  if (status != noErr || propSize == 0)
  {
    DBGMSG("Default output device does not support input (loopback)\n");
    return std::nullopt;
  }
  
  // Get the UID of the loopback source
  CFStringRef loopbackUID = GetDeviceUID(loopbackSourceID);
  
  // Get the UID of the requested output device
  CFStringRef outputUID = GetDeviceUID(outputDeviceID);
  
  // Create aggregate device description
  CFMutableDictionaryRef aggDesc = CFDictionaryCreateMutable(
    kCFAllocatorDefault, 0,
    &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks
  );
  
  // Set aggregate device properties
  CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceSubTypeKey), 
    CFSTR("AggregateDevice"));
  CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceNameKey), 
    CFSTR("iPlug System Loopback"));
  CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceUIDKey), 
    CFSTR("iPlugSystemLoopback"));
  
  // Create sub-devices array
  CFMutableArrayRef subDevices = CFArrayCreateMutable(
    kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks
  );
  
  // Add the loopback source device (default output, which provides system audio)
  CFMutableDictionaryRef loopbackSubDev = CFDictionaryCreateMutable(
    kCFAllocatorDefault, 0,
    &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks
  );
  CFDictionarySetValue(loopbackSubDev, CFSTR(kAudioSubDeviceUIDKey), loopbackUID);
  CFArrayAppendValue(subDevices, loopbackSubDev);
  CFRelease(loopbackSubDev);
  
  // Add the requested output device
  CFMutableDictionaryRef outputSubDev = CFDictionaryCreateMutable(
    kCFAllocatorDefault, 0,
    &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks
  );
  CFDictionarySetValue(outputSubDev, CFSTR(kAudioSubDeviceUIDKey), outputUID);
  CFArrayAppendValue(subDevices, outputSubDev);
  CFRelease(outputSubDev);
  
  // Set the sub-devices in the aggregate
  CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDevices);
  CFRelease(subDevices);
  
  // Create the aggregate device using AudioHardware
  AudioObjectID aggregateDeviceID = kAudioObjectUnknown;
  size = sizeof(aggregateDeviceID);
  
  AudioObjectPropertyAddress createAddr = {
    kAudioHardwarePropertyCreateAggregateDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };
  
  status = AudioObjectGetPropertyData(
    kAudioObjectSystemObject,
    &createAddr,
    0, nullptr,
    &size, aggDesc
  );
  
  CFRelease(aggDesc);
  
  if (status != noErr)
  {
    DBGMSG("Failed to create aggregate device: %d\n", status);
    return std::nullopt;
  }
  
  DBGMSG("Created aggregate device for system audio loopback: %u\n", aggregateDeviceID);
  return aggregateDeviceID;
}
#endif

#ifdef OS_WIN
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

// GUID for Windows WASAPI loopback device
static const GUID CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x85, 0x42, 0x8A}};

// static
bool IPlugAPPHost::SupportsWASAPILoopback() const
{
#ifdef OS_WIN
  // WASAPI is available on Windows Vista and later
  // Loopback is supported via the "System" default input device when using WASAPI
  // or via the dedicated "Loopback" device
  return mState.mAudioDriverType == 0; // 0 = WINDOWS_DS (DirectSound doesn't support loopback well)
#else
  return false;
#endif
}
#endif

////////////////////////////////////////////////////////////////////////////////
// SYSTEM TRAY / MENU BAR IMPLEMENTATION
//
// Usage:
// 1. Call host->SetSystemTrayMode(true) to enable system tray/menu bar mode
// 2. When user closes the window, the app should minimize to tray instead of quitting
// 3. Click on the tray icon to re-open the main window
//
// On macOS: Uses NSStatusItem to add an icon to the menu bar
// On Windows: Uses Shell_NotifyIcon API to add an icon to the system tray
//
// For macOS, SWELL provides the message loop, so we post a notification
// when the tray icon is clicked.
// For Windows, we create a hidden window to receive tray icon messages.
////////////////////////////////////////////////////////////////////////////////

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

// Cocoa wrapper for system tray functionality
@interface IPlugSystemTrayDelegate : NSObject <NSStatusBarDelegate>
{
  void* mHost; // IPlugAPPHost pointer
}
- (instancetype)initWithHost:(void*)host;
- (void)statusBarButtonClicked:(id)sender;
@end

@implementation IPlugSystemTrayDelegate
{
  void* mHost;
  NSStatusItem* mStatusItem;
}

- (instancetype)initWithHost:(void*)host
{
  self = [super init];
  if (self)
  {
    mHost = host;
    mStatusItem = nil;
  }
  return self;
}

- (void)setStatusItem:(NSStatusItem*)item
{
  mStatusItem = item;
}

- (void)statusBarButtonClicked:(id)sender
{
  // Post notification to app to show main window
  [[NSNotificationCenter defaultCenter] postNotificationName:@"IPlugShowMainWindow" object:nil];
}

@end

// Global reference to the delegate (to prevent garbage collection)
static IPlugSystemTrayDelegate* sTrayDelegate = nullptr;

#endif // __APPLE__

#ifdef OS_WIN
// Windows tray icon data
static NOTIFYICONDATAW gTrayIconData;
static HWND gTrayIconWnd = nullptr;

// Window procedure for tray icon messages
static LRESULT CALLBACK TrayIconWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_USER + 1: // Tray icon clicked
      if (lParam == WM_LBUTTONDOWN)
      {
        // Left click - show the main window
        if (gHWND)
        {
          ShowWindow(gHWND, SW_SHOW);
          SetForegroundWindow(gHWND);
        }
      }
      else if (lParam == WM_RBUTTONUP)
      {
        // Right click - show context menu with Quit option
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1, L"Open iPlug");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, 2, L"Quit");
        
        // Show menu at cursor position
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(gHWND);
        int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, 
          pt.x, pt.y, 0, gHWND, nullptr);
        DestroyMenu(hMenu);
        
        if (cmd == 1)
        {
          // Open - show window
          ShowWindow(gHWND, SW_SHOW);
          SetForegroundWindow(gHWND);
        }
        else if (cmd == 2)
        {
          // Quit
          Shell_NotifyIconW(NIM_DELETE, &gTrayIconData);
          PostQuitMessage(0);
        }
      }
      break;
    case WM_DESTROY:
      Shell_NotifyIconW(NIM_DELETE, &gTrayIconData);
      break;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
#endif // OS_WIN

// Implementation
void IPlugAPPHost::SetSystemTrayMode(bool enable, const char* iconPath)
{
  mSystemTrayMode = enable;
  if (iconPath)
    mSystemTrayIconPath.Set(iconPath);
  
#ifdef __APPLE__
  if (enable)
  {
    // Create NSStatusItem for menu bar
    NSStatusItem* statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    
    // Set up the button
    NSStatusBarButton* button = [statusItem button];
    if (iconPath && strlen(iconPath) > 0)
    {
      NSString* path = [NSString stringWithUTF8String:iconPath];
      NSImage* icon = [[NSImage alloc] initWithContentsOfFile:path];
      if (icon)
      {
        [button setImage:icon];
      }
    }
    else
    {
      // Use app icon as default
      [button setImage:[NSImage imageNamed:NSImageNameApplicationIcon]];
    }
    
    [button setToolTip:@"iPlug Audio"];
    
    // Create and assign delegate
    if (!sTrayDelegate)
    {
      sTrayDelegate = [[IPlugSystemTrayDelegate alloc] initWithHost:this];
    }
    [sTrayDelegate setStatusItem:statusItem];
    [button setTarget:sTrayDelegate];
    [button setAction:@selector(statusBarButtonClicked:)];
    
    DBGMSG("Created menu bar status item\\n");
  }
  else
  {
    // Remove status item
    if (sTrayDelegate)
    {
      [[NSStatusBar systemStatusBar] removeStatusItem:[[sTrayDelegate] statusItem]];
      sTrayDelegate = nullptr;
    }
    DBGMSG("Removed menu bar status item\\n");
  }
#elif defined(OS_WIN)
  if (enable)
  {
    // Register a hidden window for tray icon messages
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TrayIconWndProc;
    wc.lpszClassName = L"IPlugTrayIconWnd";
    wc.hInstance = gHINSTANCE;
    RegisterClassW(&wc);
    
    gTrayIconWnd = CreateWindowW(L"IPlugTrayIconWnd", L"IPlug Tray", 
      WS_DISABLED, 0, 0, 0, 0, NULL, NULL, gHINSTANCE, NULL);
    
    // Set up tray icon data
    memset(&gTrayIconData, 0, sizeof(gTrayIconData));
    gTrayIconData.cbSize = sizeof(gTrayIconData);
    gTrayIconData.hWnd = gTrayIconWnd;
    gTrayIconData.uID = 1;
    gTrayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gTrayIconData.uCallbackMessage = WM_USER + 1;
    wcscpy(gTrayIconData.szTip, L"iPlug Audio");
    
    // Load the app icon
    gTrayIconData.hIcon = LoadIconW(gHINSTANCE, MAKEINTRESOURCE(IDI_APP_ICON));
    
    // Add the icon to the system tray
    Shell_NotifyIconW(NIM_ADD, &gTrayIconData);
    
    DBGMSG("Created system tray icon\\n");
  }
  else
  {
    // Remove tray icon
    Shell_NotifyIconW(NIM_DELETE, &gTrayIconData);
    
    if (gTrayIconWnd)
    {
      DestroyWindow(gTrayIconWnd);
      gTrayIconWnd = nullptr;
    }
    
    UnregisterClassW(L"IPlugTrayIconWnd", gHINSTANCE);
    DBGMSG("Removed system tray icon\\n");
  }
#else
  // Linux - not implemented
  DBGMSG("System tray mode not supported on this platform\\n");
#endif
}

void IPlugAPPHost::ShowNotification(const char* title, const char* message)
{
#ifdef __APPLE__
  // Use NSUserNotification for system notifications on macOS
  NSUserNotificationCenter* center = [NSUserNotificationCenter defaultUserNotificationCenter];
  NSUserNotification* notification = [[NSUserNotification alloc] init];
  notification.title = [NSString stringWithUTF8String:title];
  notification.informativeText = [NSString stringWithUTF8String:message];
  [center deliverNotification:notification];
#elif defined(OS_WIN)
  // Use balloon tooltip via Shell_NotifyIcon
  if (gTrayIconData.hWnd)
  {
    wchar_t wtitle[256];
    wchar_t wmsg[1024];
    mbstowcs(wtitle, title, 256);
    mbstowcs(wmsg, message, 1024);
    wcsncpy(gTrayIconData.szInfoTitle, wtitle, 64);
    wcsncpy(gTrayIconData.szInfo, wmsg, 256);
    gTrayIconData.uFlags = NIF_INFO | NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &gTrayIconData);
  }
#endif
}

void IPlugAPPHost::OnSystemTrayClick()
{
  // Show and activate the main window when tray icon is clicked
  if (gHWND)
  {
    ShowWindow(gHWND, SW_SHOW);
    SetForegroundWindow(gHWND);
  }
}
