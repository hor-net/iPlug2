/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once

#ifdef AAX_API
#include "IPlugAAX_view_interface.h"
#endif

#include "IPlugEditorDelegate.h"
#include "IPlugWebView.h"
#include "wdl_base64.h"
#include "json.hpp"
#include <functional>

BEGIN_IPLUG_NAMESPACE

#ifndef DEFAULT_PATH
static const char* DEFAULT_PATH = "~/Desktop";
#endif

// calculate the size of 'output' buffer required for a 'input' buffer of length x during Base64 encoding operation
#define B64ENCODE_OUT_SAFESIZE(x) ((((x) + 3 - 1)/3) * 4 + 1)

// calculate the size of 'output' buffer required for a 'input' buffer of length x during Base64 decoding operation
#define B64DECODE_OUT_SAFESIZE(x) (((x)*3)/4)


/** This Editor Delegate allows using a platform native web view as the UI for an iPlug plugin */
class WebViewEditorDelegate : public IEditorDelegate
                            , public IWebView
{
  static constexpr int kDefaultMaxJSStringLength = 1048576;
  
public:
  WebViewEditorDelegate(int nParams);
  virtual ~WebViewEditorDelegate();
  
  //IEditorDelegate
  void* OpenWindow(void* pParent) override;

#ifdef OS_MAC
  void CloseWindow() override;
#endif
  
#ifdef OS_WIN
  void CloseWindow() override
  {
    CloseWebView();
  }
#endif
  
  void SendControlValueFromDelegate(int ctrlTag, double normalizedValue) override
  {
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SCVFD(%i, %f)", ctrlTag, normalizedValue);
    EvaluateJavaScript(str.Get());
  }

  void SendControlMsgFromDelegate(int ctrlTag, int msgTag, int dataSize, const void* pData) override
  {
    WDL_String str;
    std::vector<char> base64;
    base64.resize(B64ENCODE_OUT_SAFESIZE(dataSize));
    wdl_base64encode(reinterpret_cast<const unsigned char*>(pData), base64.data(), dataSize);
    str.SetFormatted(mMaxJSStringLength, "SCMFD(%i, %i, %i, \"%s\")", ctrlTag, msgTag, base64.size(), base64.data());
    EvaluateJavaScript(str.Get());
  }

  void SendParameterValueFromDelegate(int paramIdx, double value, bool normalized) override
  {
    if(!normalized) {
      value = GetParam(paramIdx)->ToNormalized(value);
    }
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SPVFD(%i, %f)", paramIdx, value);
    EvaluateJavaScript(str.Get());
  }

  void SendArbitraryMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    WDL_String str;
    std::vector<char> base64;
    base64.resize(B64ENCODE_OUT_SAFESIZE(dataSize));
    wdl_base64encode(reinterpret_cast<const unsigned char*>(pData), base64.data(), dataSize);
    str.SetFormatted(mMaxJSStringLength, "SAMFD(%i, %lu, \"%s\")", msgTag, base64.size(), base64.data());
    EvaluateJavaScript(str.Get());
    
  }
  
  void SendMidiMsgFromDelegate(const IMidiMsg& msg) override
  {
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SMMFD(%i, %i, %i)", msg.mStatus, msg.mData1, msg.mData2);
    EvaluateJavaScript(str.Get());
  }

  void OnMessageFromWebView(const char* jsonStr) override
  {
    nlohmann::json json;
    try {
      json = nlohmann::json::parse(jsonStr, nullptr, false);
    } catch (nlohmann::json::exception& e) {
      return;
    }
    
    if(json["msg"] == "SPVFUI")
    {
      SendParameterValueFromUI(json["paramIdx"], json["value"]);
    }
    else if (json["msg"] == "BPCFUI")
    {
      BeginInformHostOfParamChangeFromUI(json["paramIdx"]);
    }
    else if (json["msg"] == "EPCFUI")
    {
      EndInformHostOfParamChangeFromUI(json["paramIdx"]);
    }
    else if (json["msg"] == "SAMFUI")
    {
      std::vector<unsigned char> base64;
      
      if(json.count("data") > 0 && json["data"].is_string())
      {
        auto dStr = json["data"].get<std::string>();
        int dSize = static_cast<int>(dStr.size());
        base64.resize(B64DECODE_OUT_SAFESIZE(dSize));
        wdl_base64decode(dStr.c_str(), base64.data(), static_cast<int>(base64.size()));
        SendArbitraryMsgFromUI(json["msgTag"], json["ctrlTag"], static_cast<int>(base64.size()), base64.data());
      }
      
    }
    else if(json["msg"] == "SMMFUI")
    {
      IMidiMsg msg {0, json["statusByte"].get<uint8_t>(),
                       json["dataByte1"].get<uint8_t>(),
                       json["dataByte2"].get<uint8_t>()};
      SendMidiMsgFromUI(msg);
    }
  #ifdef OS_WIN
    else if (json["msg"] == "keypress")
    {
      if (json["data"].is_number_integer())
      {
        if (json["data"] == 32) { // spacebar
          PostMessage(mParentWnd, WM_KEYDOWN, VK_SPACE, 0);
        }
      }
    }
   #endif
  }

  void Resize(int width, int height);

  void OnWebViewReady() override
  {
#ifdef OS_WIN
    if (mScale > 1.)
      SetEditorSize(GetEditorWidth() / mScale, GetEditorHeight() / mScale);
#endif
    if (mEditorInitFunc)
      mEditorInitFunc();
  }
  
  void OnWebContentLoaded() override
  {
    for (int i = 0; i < NParams(); i++) {
      WDL_String json;
      GetParam(i)->GetJSON(json, i);
      SendControlMsgFromDelegate(i, -1, sizeof(char)*json.GetLength(), (void*)json.Get());
    }
    OnUIOpen();
  }
  
  void SetMaxJSStringLength(int length)
  {
    mMaxJSStringLength = length;
  }

  void SetEnableDevTools(bool enable)
  {
    mEnableDevTools = enable;
  }
  
  bool GetEnableDevTools() const { return mEnableDevTools; }

protected:
  
  int GetBase64Length(int dataSize)
  {
    return static_cast<int>(4. * std::ceil((static_cast<double>(dataSize) / 3.)));
  }
  
  int mMaxJSStringLength = kDefaultMaxJSStringLength;
  std::function<void()> mEditorInitFunc = nullptr;
  void* mHelperView = nullptr;
  float mScale;
  
private:
  bool mEnableDevTools = false;
};

END_IPLUG_NAMESPACE
