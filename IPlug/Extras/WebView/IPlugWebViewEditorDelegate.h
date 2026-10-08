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
#include "IPlugWebViewDiagnostics.h"
#include "wdl_base64.h"
#include "json.hpp"
#include <functional>
#include <algorithm>
#include <filesystem>
#include <queue>
#include <mutex>
#include <string>

#ifdef VST3_API
#include "pluginterfaces/vst/ivstcontextmenu.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "pluginterfaces/base/funknown.h"
#endif

/**
 * @file
 * @copydoc WebViewEditorDelegate
 */

BEGIN_IPLUG_NAMESPACE

#ifdef OS_MAC
// Native AppKit overlay kept above WKWebView so it can communicate loading
// failures even when WebKit's content surface has not painted.
void UpdateNativeWebViewLoadingOverlay(void* pView, int state, int timeoutMs, const char* details);
#endif

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
    TraceWebView(static_cast<IWebView*>(this), "editor-close", "ready=%d opened=%d", mWebViewReady, mUIOpenDone);
    if (mUIOpenDone)
      OnUIClose();
#ifdef OS_MAC
    UpdateNativeOverlay(0, "");
#endif
    CloseWebView();
#ifdef OS_MAC
    {
      std::lock_guard<std::mutex> lock(mQueueMutex);
      std::queue<std::string> empty;
      mJavaScriptQueue.swap(empty);
    }
#endif
    mReadyDocumentId.clear();
    // Per-open handshake state: the editor init must run exactly once per
    // open and the ready sequence must restart on the next one.
    mWebViewReady = false;
    mEditorInitDone = false;
    mUIOpenDone = false;
    mEditorViewAttached = false;
    mEditorWebViewCreated = false;
  }

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override
  {
#ifdef VST3_API
    if (msgTag == 0x56535433 && dataSize == sizeof(void*) * 2 && pData)
    {
      void** ptrs = (void**)const_cast<void*>(pData);
      mHandler = (Steinberg::Vst::IComponentHandler*)ptrs[0];
      mVST3View = (Steinberg::IPlugView*)ptrs[1];
      return true;
    }
#endif
    return false;
  }

  void SendControlValueFromDelegate(int ctrlTag, double normalizedValue) override
  {
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SCVFD(%i, %f)", ctrlTag, normalizedValue);
   #ifdef OS_MAC
    QueueJavaScript(str.Get());
  #else
    EvaluateJavaScript(str.Get());
   #endif
  }

  void SendControlMsgFromDelegate(int ctrlTag, int msgTag, int dataSize, const void* pData) override
  {
    WDL_String str;
    std::vector<char> base64;
    base64.resize(B64ENCODE_OUT_SAFESIZE(dataSize));
    wdl_base64encode(reinterpret_cast<const unsigned char*>(pData), base64.data(), dataSize);
    str.SetFormatted(mMaxJSStringLength, "SCMFD(%i, %i, %i, \"%s\")", ctrlTag, msgTag, base64.size(), base64.data());
#ifdef OS_MAC
    QueueJavaScript(str.Get());
#else
    EvaluateJavaScript(str.Get());
#endif
  }

  void SendParameterValueFromDelegate(int paramIdx, double value, bool normalized) override
  {
    WDL_String str;
    
    if (!normalized)
    {
      value = GetParam(paramIdx)->ToNormalized(value);
    }
    
    str.SetFormatted(mMaxJSStringLength, "SPVFD(%i, %f)", paramIdx, value);
#ifdef OS_MAC
    QueueJavaScript(str.Get());
#else
    EvaluateJavaScript(str.Get());
#endif
  }

  void SendArbitraryMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    WDL_String str;
    std::vector<char> base64;
    base64.resize(B64ENCODE_OUT_SAFESIZE(dataSize));
    wdl_base64encode(reinterpret_cast<const unsigned char*>(pData), base64.data(), dataSize);
    str.SetFormatted(mMaxJSStringLength, "SAMFD(%i, %lu, \"%s\")", msgTag, base64.size(), base64.data());
#ifdef OS_MAC
    QueueJavaScript(str.Get());
#else
    EvaluateJavaScript(str.Get());
#endif
    
  }
  
  void SendMidiMsgFromDelegate(const IMidiMsg& msg) override
  {
    WDL_String str;
    str.SetFormatted(mMaxJSStringLength, "SMMFD(%i, %i, %i)", msg.mStatus, msg.mData1, msg.mData2);
#ifdef OS_MAC
    QueueJavaScript(str.Get());
#else
    EvaluateJavaScript(str.Get());
#endif
  }
  
  bool OnKeyDown(const IKeyPress& key) override;
  bool OnKeyUp(const IKeyPress& key) override;

  // IWebView

  void SendJSONFromDelegate(const nlohmann::json& jsonMessage)
  {
    SendArbitraryMsgFromDelegate(-1, static_cast<int>(jsonMessage.dump().size()), jsonMessage.dump().c_str());
  }

  /** Called when a right click occurs in the webview. 
   *  paramIdx is the parameter index or -1. 
   *  x, y are coordinates in the webview (scaled by dpr).
   *  dpr is the device pixel ratio. 
   *  Override this in your plugin class to handle the context menu (e.g. using IComponentHandler3 in VST3).
   */
  virtual void OnWebContextMenu(int paramIdx, float x, float y, float dpr)
  {
#ifdef VST3_API
    if (mHandler && mVST3View)
    {
      Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler3> handler3(mHandler);
      if (handler3)
      {
        Steinberg::Vst::IContextMenu* pContextMenu = nullptr;
        Steinberg::Vst::ParamID pid = static_cast<Steinberg::Vst::ParamID>(paramIdx);
        pContextMenu = handler3->createContextMenu(mVST3View, (paramIdx >= 0) ? &pid : nullptr);
        if (pContextMenu)
              {
                float scale = 1.f;
#ifdef OS_WIN
                scale = dpr;
#endif
                pContextMenu->popup(x * scale, y * scale);
                pContextMenu->release();
              }
      }
    }
#endif
  }

#ifdef VST3_API
  Steinberg::Vst::IComponentHandler* mHandler = nullptr;
  Steinberg::IPlugView* mVST3View = nullptr;
#endif

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
      try {
        SendParameterValueFromUI(json["paramIdx"], json["value"]);
      } catch (nlohmann::json::exception& e) {
        return;
      }  
      
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
    else if(json["msg"] == "CTXMFUI")
    {
      int paramIdx = json["paramIdx"].get<int>();
      float x = json["x"].get<float>();
      float y = json["y"].get<float>();
      float dpr = json["dpr"].get<float>();
      OnWebContextMenu(paramIdx, x, y, dpr);
    }
#ifdef _DEBUG
    else if(json["msg"] == "WEBVIEW_DIAGNOSTIC")
    {
      const std::string event = json.value("event", std::string{});
      if (event == "page-error" || event == "grid-context-lost" || event == "grid-context-restored"
          || event == "spectrum-context-lost" || event == "spectrum-context-restored")
        TraceWebView(static_cast<IWebView*>(this), event.c_str(), "line=%d column=%d",
          json.value("line", 0), json.value("column", 0));
    }
#endif
    else if(json["msg"] == "JSREADY")
    {
      // Retries belong to one document. A reload creates a new document and
      // must get a new full snapshot, even while the native editor stays open.
      const std::string documentId = json.value("documentId", std::string{});
      TraceWebView(static_cast<IWebView*>(this), "js-ready", "duplicate=%d ready=%d opened=%d",
        documentId == mReadyDocumentId, mWebViewReady, mUIOpenDone);
      if (!documentId.empty() && documentId != mReadyDocumentId)
      {
        mReadyDocumentId = documentId;
        mWebViewReady = false;
        mUIOpenDone = false;
      }
      OnWebContentLoaded();
    }
  }

  void Resize(int width, int height);
  
  void OnParentWindowResize(int width, int height) override;

  /** Enable a native macOS loading/failure overlay for this WebView editor. */
  void EnableNativeLoadingOverlay(bool enable, int timeoutMs = 5000)
  {
#ifdef OS_MAC
    mNativeLoadingOverlayEnabled = enable;
    mNativeLoadingTimeoutMs = std::max(100, timeoutMs);
#else
    (void) enable;
    (void) timeoutMs;
#endif
  }

#ifdef OS_MAC
  bool IsNativeLoadingOverlayEnabled() const { return mNativeLoadingOverlayEnabled; }
  std::string GetNativeLoadingDiagnostics() const
  {
    const auto elapsedMs = mNativeLoadStartedAt.time_since_epoch().count() == 0 ? 0LL :
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - mNativeLoadStartedAt).count();
    char info[1024];
    std::snprintf(info, sizeof(info),
      "Waited: %lld ms\nNavigation started: %s\nNavigation committed: %s\nNavigation finished: %s\nNavigation failure reported: %s\nContent process terminated: %s\nWebKit error: %s (%d)",
      static_cast<long long>(elapsedMs), mNativeNavigationStarted ? "yes" : "no",
      mNativeNavigationCommitted ? "yes" : "no", mNativeNavigationFinished ? "yes" : "no",
      mNativeLoadFailed ? "yes" : "no", mNativeContentProcessTerminated ? "yes" : "no",
      mNativeErrorDomain.empty() ? "none" : mNativeErrorDomain.c_str(), mNativeErrorCode);
    return info;
  }
#endif

  // Start the page load only when BOTH the webview exists and the editor view
  // is attached to its parent window, exactly once per open. Loading earlier
  // (webview created but not yet hosted) is unreliable in some hosts and the
  // editor then stays a blank white surface; the historical double call from
  // OpenWindow masked this by loading again after the attach.
  void TryStartEditorInit()
  {
    if (mEditorInitDone || !mEditorInitFunc || !mEditorViewAttached || !mEditorWebViewCreated)
      return;
    mEditorInitDone = true;
    TraceWebView(static_cast<IWebView*>(this), "editor-init");
#ifdef OS_MAC
    if (mNativeLoadingOverlayEnabled)
    {
      mNativeLoadStartedAt = std::chrono::steady_clock::now();
      mNativeNavigationStarted = false;
      mNativeNavigationCommitted = false;
      mNativeNavigationFinished = false;
      mNativeLoadFailed = false;
      mNativeContentProcessTerminated = false;
      mNativeErrorDomain.clear();
      mNativeErrorCode = 0;
      UpdateNativeOverlay(1, "Waiting for the WebView document to become ready.");
    }
#endif
    mEditorInitFunc();
  }

  void OnWebViewReady() override
  {
    mEditorWebViewCreated = true;
#ifdef OS_WIN
    // WebView2 creates its controller directly in the supplied parent HWND;
    // it has no macOS helper-view attachment step.
    mEditorViewAttached = true;
#endif
    TryStartEditorInit();
  }
  
  void OnWebContentLoading() override
  {
    // The old document's readiness cannot authorize calls into the new one.
    // Keep the native UI-open flag until JSREADY (or close) so OnUIClose still
    // runs if the host closes an editor while its navigation is in progress.
    mWebViewReady = false;
    mReadyDocumentId.clear();
    TraceWebView(static_cast<IWebView*>(this), "document-loading");
#ifdef OS_MAC
    if (mNativeLoadingOverlayEnabled)
    {
      const bool newAttempt = (mNativeOverlayState == 0);
      if (newAttempt)
        mNativeLoadStartedAt = std::chrono::steady_clock::now();
      mNativeNavigationStarted = true;
      mNativeNavigationCommitted = false;
      mNativeNavigationFinished = false;
      mNativeLoadFailed = false;
      mNativeContentProcessTerminated = false;
      mNativeErrorDomain.clear();
      mNativeErrorCode = 0;
      UpdateNativeOverlay(newAttempt ? 1 : 3, "WebKit navigation started; waiting for JavaScript readiness.");
    }
#endif
  }

  void OnWebContentNavigationCommitted() override
  {
#ifdef OS_MAC
    if (mNativeLoadingOverlayEnabled)
    {
      mNativeNavigationCommitted = true;
      UpdateNativeOverlay(3, "WebKit committed the document; waiting for JavaScript readiness.");
    }
#endif
  }

  void OnWebContentNavigationFinished() override
  {
#ifdef OS_MAC
    if (mNativeLoadingOverlayEnabled)
    {
      mNativeNavigationFinished = true;
      UpdateNativeOverlay(3, "WebKit finished navigation; waiting for JavaScript readiness.");
    }
#endif
  }

  void OnWebContentLoadFailed(const char* errorDomain, int errorCode, bool provisional) override
  {
#ifdef OS_MAC
    if (mNativeLoadingOverlayEnabled)
    {
      mNativeLoadFailed = true;
      mNativeErrorDomain = errorDomain ? errorDomain : "unknown";
      mNativeErrorCode = errorCode;
      UpdateNativeOverlay(2, provisional ? "WebKit provisional navigation failed." : "WebKit navigation failed.");
    }
#else
    (void) errorDomain;
    (void) errorCode;
    (void) provisional;
#endif
  }

  void OnWebContentProcessTerminated() override
  {
#ifdef OS_MAC
    if (mNativeLoadingOverlayEnabled)
    {
      const bool wasReady = (mNativeOverlayState == 0);
      mNativeContentProcessTerminated = true;
      UpdateNativeOverlay(2, wasReady ?
        "The WebKit content process terminated after the UI became ready." :
        "The WebKit content process terminated before the UI became ready.");
    }
#endif
  }

  void OnWebContentLoaded() override
  {
#ifdef OS_WIN
    // WebView2 calls this once per successful NavigationCompleted, not from
    // the macOS JSREADY retry loop. Every navigation needs a full snapshot.
    mUIOpenDone = false;
#else
    // Duplicate notifications must not reapply metadata defaults over the
    // host's values. New documents reset these flags in the JSREADY handler.
    if (mWebViewReady && mUIOpenDone)
      return;
#endif

    mWebViewReady = true;
#ifdef OS_MAC
    if (mNativeLoadingOverlayEnabled)
      UpdateNativeOverlay(0, "");
#endif
    TraceWebView(static_cast<IWebView*>(this), "send-snapshot", "params=%d", NParams());
    
    // Now prepare and send the params message (this will execute immediately since mWebViewReady is now true)
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

    // Send params using the correct mechanism (SendJSONFromDelegate -> SendArbitraryMsgFromDelegate with -1)
    SendJSONFromDelegate(msg);

   #ifdef OS_MAC
    // First flush all queued JavaScript messages and set mWebViewReady = true
    FlushJavaScriptQueue();
    // Acknowledge the JSREADY retries so the injected ping loop can stop.
    QueueJavaScript("try { window.IPlugJsAck = 1; } catch (e) {}");
   #endif

    if (!mUIOpenDone)
    {
      mUIOpenDone = true;
      OnUIOpen();
    }
  }
  
  void SetMaxJSStringLength(int length)
  {
    mMaxJSStringLength = length;
  }

  #ifdef OS_MAC
  // JavaScript message queue system for macOS timing fix
  void QueueJavaScript(const char* scriptStr);
  void FlushJavaScriptQueue();
  #endif

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
  
  // JavaScript message queue system for macOS timing fix
  std::queue<std::string> mJavaScriptQueue;
  bool mWebViewReady = false;
  bool mEditorInitDone = false;
  bool mUIOpenDone = false;
  bool mEditorViewAttached = false;
  bool mEditorWebViewCreated = false;
  std::string mReadyDocumentId;
#ifdef OS_MAC
  bool mNativeLoadingOverlayEnabled = false;
  int mNativeLoadingTimeoutMs = 5000;
  int mNativeOverlayState = 0;
  std::chrono::steady_clock::time_point mNativeLoadStartedAt{};
  bool mNativeNavigationStarted = false;
  bool mNativeNavigationCommitted = false;
  bool mNativeNavigationFinished = false;
  bool mNativeLoadFailed = false;
  bool mNativeContentProcessTerminated = false;
  std::string mNativeErrorDomain;
  int mNativeErrorCode = 0;

  void UpdateNativeOverlay(int state, const char* details)
  {
    if (!mNativeLoadingOverlayEnabled)
      return;
    if (state != 3)
      mNativeOverlayState = state;
    UpdateNativeWebViewLoadingOverlay(mView, state, mNativeLoadingTimeoutMs, details);
  }
#endif
  std::mutex mQueueMutex;
  
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
