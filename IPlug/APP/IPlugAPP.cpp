/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include "IPlugAPP.h"
#include "IPlugAPP_host.h"
#include "IVTransportControl.h"

#if defined OS_MAC || defined OS_LINUX
#include <IPlugSWELL.h>
#else
extern float GetScaleForHWND(HWND hWnd);
#endif

using namespace iplug;

//class IGraphics;
#include "IGraphics.h"

extern HWND gHWND;

IPlugAPP::IPlugAPP(const InstanceInfo& info, const Config& config)
: IPlugAPIBase(config, kAPIAPP)
, IPlugProcessor(config, kAPIAPP)
{
  mAppHost = (IPlugAPPHost*) info.pAppHost;
  
  Trace(TRACELOC, "%s%s", config.pluginName, config.channelIOStr);

  SetChannelConnections(ERoute::kInput, 0, MaxNChannels(ERoute::kInput), true);
  SetChannelConnections(ERoute::kOutput, 0, MaxNChannels(ERoute::kOutput), true);

  SetBlockSize(DEFAULT_BLOCK_SIZE);
  
  CreateTimer();
}

IPlugAPP::~IPlugAPP()
{
#if APP_HAS_TRANSPORT_BAR
  if(mTransportTimer)
  {
    mTransportTimer->Stop();
  }
#endif
}

bool IPlugAPP::EditorResize(int viewWidth, int viewHeight)
{
  bool parentResized = false;
    
  if (viewWidth != GetEditorWidth() || viewHeight != GetEditorHeight())
  {
    #if defined OS_MAC || defined NO_IGRAPHICS 
    RECT rcClient, rcWindow;
    POINT ptDiff;
    
    GetClientRect(gHWND, &rcClient);
    GetWindowRect(gHWND, &rcWindow);
    
    ptDiff.x = (rcWindow.right - rcWindow.left) - rcClient.right;
    ptDiff.y = (rcWindow.bottom - rcWindow.top) - rcClient.bottom;
    
    int flags = 0;
    
    #ifdef OS_WIN
    flags = SWP_NOMOVE;
    float ss = GetScaleForHWND(gHWND);
    #else
    float ss = 1.f;
    #endif
    
    SetWindowPos(gHWND, 0,
                 static_cast<LONG>(rcWindow.left * ss),
                 static_cast<LONG>((rcWindow.bottom - viewHeight - ptDiff.y) * ss),
                 static_cast<LONG>((viewWidth + ptDiff.x) * ss),
                 static_cast<LONG>((viewHeight + ptDiff.y) * ss), flags);
    parentResized = true;
    #endif
    
    SetEditorSize(viewWidth, viewHeight);
  }
  
  return parentResized;
}

bool IPlugAPP::SendMidiMsg(const IMidiMsg& msg)
{
  if (DoesMIDIOut() && mAppHost->mMidiOut)
  {
    //TODO: midi out channel
//    uint8_t status;
//
//    // if the midi channel out filter is set, reassign the status byte appropriately
//    if(mAppHost->mMidiOutChannel > -1)
//      status = mAppHost->mMidiOutChannel-1 | ((uint8_t) msg.StatusMsg() << 4) ;

    std::vector<uint8_t> message;
    message.push_back(msg.mStatus);
    
    // only send midi data if message is not midi clock, start and  stop
    if(msg.mStatus != 0xF8 && msg.mStatus != 0xFA && msg.mStatus != 0xFC) {
      message.push_back(msg.mData1);
      message.push_back(msg.mData2);
    }
    

    mAppHost->mMidiOut->sendMessage(&message);
    
    return true;
  }

  return false;
}

bool IPlugAPP::SendSysEx(const ISysEx& msg)
{
  if (DoesMIDIOut() && mAppHost->mMidiOut)
  {
    //TODO: midi out channel
    std::vector<uint8_t> message;
    
    for (int i = 0; i < msg.mSize; i++)
    {
      message.push_back(msg.mData[i]);
    }
    
    mAppHost->mMidiOut->sendMessage(&message);
    return true;
  }
  
  return false;
}

void IPlugAPP::SendSysexMsgFromUI(const ISysEx& msg)
{
  SendSysEx(msg);
}

void IPlugAPP::AppProcess(double** inputs, double** outputs, int nFrames)
{
  SetChannelConnections(ERoute::kInput, 0, MaxNChannels(ERoute::kInput), !IsInstrument()); //TODO: go elsewhere - enable inputs
  SetChannelConnections(ERoute::kOutput, 0, MaxNChannels(ERoute::kOutput), true); //TODO: go elsewhere
  AttachBuffers(ERoute::kInput, 0, NChannelsConnected(ERoute::kInput), inputs, GetBlockSize());
  AttachBuffers(ERoute::kOutput, 0, NChannelsConnected(ERoute::kOutput), outputs, GetBlockSize());
  
  if(mMidiMsgsFromCallback.ElementsAvailable())
  {
    IMidiMsg msg;
    
    while (mMidiMsgsFromCallback.Pop(msg))
    {
      ProcessMidiMsg(msg);
      mMidiMsgsFromProcessor.Push(msg); // queue incoming MIDI for UI
    }
  }
  
  if(mSysExMsgsFromCallback.ElementsAvailable())
  {
    SysExData data;
    
    while (mSysExMsgsFromCallback.Pop(data))
    {
      ISysEx msg { data.mOffset, data.mData, data.mSize };
      ProcessSysEx(msg);
      mSysExDataFromProcessor.Push(data); // queue incoming Sysex for UI
    }
  }
  
  if(mMidiMsgsFromEditor.ElementsAvailable())
  {
    IMidiMsg msg;

    while (mMidiMsgsFromEditor.Pop(msg))
    {
      ProcessMidiMsg(msg);
    }
  }

  //Do not handle Sysex messages here - SendSysexMsgFromUI overridden

  ENTER_PARAMS_MUTEX
  ProcessBuffers(0.0, GetBlockSize());
  LEAVE_PARAMS_MUTEX
}

#if APP_HAS_TRANSPORT_BAR
void IPlugAPP::LayoutUI(IGraphics* pGraphics)
{
  if(mLayoutFunc)
    mLayoutFunc(pGraphics);
  
  
  for (int i = 0; i < GetUI()->NControls(); i++) {
    IRECT controlRect = GetUI()->GetControl(i)->GetRECT();
    controlRect.Translate(0, APP_TRANSPORT_BAR_HEIGHT);
    GetUI()->GetControl(i)->SetTargetAndDrawRECTs(controlRect);
  }
  IRECT bounds = GetUI()->GetBounds();
  GetUI()->AttachControl(new IVTransportControl(IRECT(bounds.L,bounds.T,bounds.R, bounds.T+APP_TRANSPORT_BAR_HEIGHT), APP_TRANSPORT_BAR_STYLE), 999);
  
  // start a timer to update transport bar status
  mTransportTimer = std::unique_ptr<Timer>(Timer::Create(std::bind(&IPlugAPP::UpdateTransportStatus, this, std::placeholders::_1), IDLE_TIMER_RATE));
  
  mCurrentFileName.Set("untitled");
  SetWindowTitle();
  
  // toggle star and stop on spacebar press
  GetUI()->SetKeyHandlerFunc([&](const IKeyPress &key, bool isUp) -> bool {
    // 32 == spacebar
    if (key.VK == 32 && isUp) {
      mAppHost->TogglePlay(!mTimeInfo.mTransportIsRunning);
      return true;
    } else {
      return false;
    }
  });
}

bool IPlugAPP::OnMessage(int messageTag, int controlTag, int dataSize, const void* pData)
{
  
  WDL_String windowTitle;
  WDL_String fileName;
  
  switch (messageTag) {
    case IVTransportControl::EMsgTags::bpm:
      mAppHost->SetBPM(*(double*)pData);
      break;
    case IVTransportControl::EMsgTags::play:
      // we have received midi clock until now but somebody pressed play/stop
      // on our transport bar so re-enable our midi clock generation
      if(mAppHost->mMidiMaster == false) {
        mAppHost->mMidiMaster = true;
      }
      mAppHost->TogglePlay(*(bool*)pData);
      break;
    case IVTransportControl::EMsgTags::open:
      LoadPresetFromFXP((char*)pData);
      mCurrentFileName.Set((char*)pData);
      SetWindowTitle();
      break;
    case IVTransportControl::EMsgTags::save:
      SavePresetAsFXP((char*)pData);
      mCurrentFileName.Set((char*)pData);
      SetWindowTitle();
      break;
    case IVTransportControl::EMsgTags::blank:
      EMsgBoxResult res = GetUI()->ShowMessageBox("Start a new project",
                                                  "Are you sure you want to start blank \
                                                  (you will lose unsaved work)?",
                                                  EMsgBoxType::kMB_OKCANCEL);
      if (res == EMsgBoxResult::kOK) {
        DefaultParamValues();
        OnParamReset(kReset);
        OnRestoreState();
        mCurrentFileName.Set("untitled");
        SetWindowTitle();
      }
      break;
  }
  return true;
}

void IPlugAPP::UpdateTransportStatus(Timer& t)
{
  if(GetUI()) {
    IVTransportControl* transport = (IVTransportControl*)GetUI()->GetControlWithTag(999);
    transport->SetBPM(mTimeInfo.mTempo);
    transport->TogglePlay(mTimeInfo.mTransportIsRunning);
  }
}

void IPlugAPP::SetWindowTitle()
{
  WDL_String windowTitle;
  windowTitle.Set(BUNDLE_NAME);
  mCurrentFileName.remove_fileext();
  windowTitle.Append(" - ");
  const char* filename = mCurrentFileName.get_filepart();
  if(filename && !filename[0]) {
    windowTitle.Append("untitled");
  } else {
    windowTitle.Append(filename);
  }
  SetWindowText(gHWND, windowTitle.Get());
}

void IPlugAPP::GrayOutTransport(bool toggle)
{
  if(GetUI()) {
    IVTransportControl* transport = (IVTransportControl*)GetUI()->GetControlWithTag(999);
    transport->SetDisabled(toggle);
  }
}

#endif

