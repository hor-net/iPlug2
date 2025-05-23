 /*
 ==============================================================================
 
  MIT License

  iPlug2 WebView Library
  Copyright (c) 2024 Oliver Larkin

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
 
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
#include <filesystem>

/**
 * @file
 * @copydoc WebViewEditorDelegate
 */

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

  void CloseWindow() override
  {
    CloseWebView();
  }

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
    WDL_String str;
    
    if (!normalized)
    {
      value = GetParam(paramIdx)->ToNormalized(value);
    }
    
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
  
  bool OnKeyDown(const IKeyPress& key) override;
  bool OnKeyUp(const IKeyPress& key) override;

  // IWebView

  void SendJSONFromDelegate(const nlohmann::json& jsonMessage)
  {
    SendArbitraryMsgFromDelegate(-1, static_cast<int>(jsonMessage.dump().size()), jsonMessage.dump().c_str());
  }

  void OnMessageFromWebView(const char* jsonStr) override
  {
    nlohmann::json json;
    try {
      json = nlohmann::json::parse(jsonStr, nullptr, false);
    } catch (nlohmann::json::exception& e) {
      return;
    }
    
    if (json["msg"] == "SPVFUI")
    {
      assert(json["paramIdx"] > -1);
      SendParameterValueFromUI(json["paramIdx"], json["value"]);
    }
    else if (json["msg"] == "BPCFUI")
    {
      assert(json["paramIdx"] > -1);
      BeginInformHostOfParamChangeFromUI(json["paramIdx"]);
    }
    else if (json["msg"] == "EPCFUI")
    {
      assert(json["paramIdx"] > -1);
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
    else if(json["msg"] == "SKPFUI")
    {
      IKeyPress keyPress = ConvertToIKeyPress(json["keyCode"].get<uint32_t>(), json["utf8"].get<std::string>().c_str(), json["S"].get<bool>(), json["C"].get<bool>(), json["A"].get<bool>());
      json["isUp"].get<bool>() ? OnKeyUp(keyPress) : OnKeyDown(keyPress); // return value not used
    }
  }

  void Resize(int width, int height);
  
  void OnParentWindowResize(int width, int height) override;

  void OnWebViewReady() override
  {
    if (mEditorInitFunc)
    {
      mEditorInitFunc();
    }
  }
  
  void OnWebContentLoaded() override
  {
    nlohmann::json msg;
    
    msg["id"] = "params";
    std::vector<nlohmann::json> params;
    for (int idx = 0; idx < NParams(); idx++)
    {
      WDL_String jsonStr;
      IParam* pParam = GetParam(idx);
      pParam->GetJSON(jsonStr, idx);
      nlohmann::json paramMsg = nlohmann::json::parse(jsonStr.Get(), nullptr, true);
      params.push_back(paramMsg);
    }
    msg["params"] = params;

    SendJSONFromDelegate(msg);
    
    OnUIOpen();
  }
  
  void SetMaxJSStringLength(int length)
  {
    mMaxJSStringLength = length;
  }

  /** Load index.html (from plugin src dir in debug builds, and from bundle in release builds) on desktop
   * Note: if your debug build is code-signed with the hardened runtime It won't be able to load the file outside it's sandbox, and this
   * will fail.
   * On iOS, this will load index.html from the bundle
   * @param pathOfPluginSrc - path to the plugin src directory
   * @param bundleid - the bundle id, used to load the correct index.html from the bundle
   */
  void LoadIndexHtml(const char* pathOfPluginSrc, const char* bundleid)
  {
#if !defined OS_IOS && defined _DEBUG
    std::string indexRelativePath = pathOfPluginSrc;
    std::replace(indexRelativePath.begin(), indexRelativePath.end(), '\\', '/');
    auto found = indexRelativePath.find_last_of('/');
    if (found != std::string::npos)
    {
      indexRelativePath = indexRelativePath.substr(0, found);
      indexRelativePath.append("/Resources/web/index.html");
#ifdef OS_WIN
      std::replace(indexRelativePath.begin(), indexRelativePath.end(), '/', '\\');
#endif
      LoadFile(indexRelativePath.c_str(), nullptr);
    }
#else
    LoadFile("index.html", bundleid); // TODO: make this work for windows
#endif
  }

protected:
  int mMaxJSStringLength = kDefaultMaxJSStringLength;
  std::function<void()> mEditorInitFunc = nullptr;
  void* mView = nullptr;
  
private:
  IKeyPress ConvertToIKeyPress(uint32_t keyCode, const char* utf8, bool shift, bool ctrl, bool alt)
  {
    return IKeyPress(utf8, DOMKeyToVirtualKey(keyCode), shift,ctrl, alt);
  }

  static int GetBase64Length(int dataSize)
  {
    return static_cast<int>(4. * std::ceil((static_cast<double>(dataSize) / 3.)));
  }

#if defined OS_WIN
  HWND mParentWnd = NULL;
  float mScale = 1.;
  bool mNeedsWindowRescale = true;
#endif

#if defined OS_MAC || defined OS_IOS
  void ResizeWebViewAndHelper(float width, float height);
#endif
};

END_IPLUG_NAMESPACE
