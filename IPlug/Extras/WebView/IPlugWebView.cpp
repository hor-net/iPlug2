/*
==============================================================================

This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

See LICENSE.txt for  more info.

==============================================================================
*/

#include "IPlugWebView.h"
#include "IPlugPaths.h"
#include <string>
#include <windows.h>
#include <shlobj.h>
#include <cassert>
#include <filesystem>

using namespace iplug;
using namespace Microsoft::WRL;

extern float GetScaleForHWND(HWND hWnd);

IWebView::IWebView(bool opaque)
: mOpaque(opaque)
{
 
}

IWebView::~IWebView()
{
  CloseWebView();
 
  if (mDLLHandle)
  {
    FreeLibrary(mDLLHandle);
    mDLLHandle = nullptr;
  }
}

typedef HRESULT(*TCCWebView2EnvWithOptions)(
  PCWSTR browserExecutableFolder,
  PCWSTR userDataFolder,
  PCWSTR additionalBrowserArguments,
  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environment_created_handler);

void* IWebView::OpenWebView(void* pParent, float x, float y, float w, float h, float scale, bool enableDevTools)
{
  HWND hWnd = (HWND) pParent;

  float ss = GetScaleForHWND(mParentWnd);

  x *= ss;
  y *= ss;
  w *= ss;
  h *= ss;

  WDL_String cachePath;
  WebViewCachePath(cachePath);
  WCHAR cachePathWide[IPLUG_WIN_MAX_WIDE_PATH];
  UTF8ToUTF16(cachePathWide, cachePath.Get(), IPLUG_WIN_MAX_WIDE_PATH);

  CreateCoreWebView2EnvironmentWithOptions(
    nullptr, cachePathWide, nullptr,
  Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
    [&, x, y, w, h](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
      env
        ->CreateCoreWebView2Controller(
          mParentWnd,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
          [&, x, y, w, h, enableDevTools](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (controller != nullptr)
            {
              mWebViewCtrlr = controller;
              mWebViewCtrlr->get_CoreWebView2(&mWebViewWnd);
            }

            ICoreWebView2Settings* Settings;
            mWebViewWnd->get_Settings(&Settings);
            Settings->put_IsScriptEnabled(TRUE);
            Settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            Settings->put_IsWebMessageEnabled(TRUE);
            Settings->put_AreDefaultContextMenusEnabled(FALSE);
            Settings->put_AreDevToolsEnabled(FALSE);
            #ifdef _DEBUG
            Settings->put_AreDefaultContextMenusEnabled(TRUE);
            Settings->put_AreDevToolsEnabled(TRUE);
            #endif




            // this script adds a function IPlugSendMsg that is used to call the platform webview messaging function in JS
            mWebViewWnd->AddScriptToExecuteOnDocumentCreated(L"function IPlugSendMsg(m) {window.chrome.webview.postMessage(m)};",
              Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [this](HRESULT error, PCWSTR id) -> HRESULT {
                  return S_OK;
                }).Get());

            // this script captures the spacebar key press and forward it to the host app
            mWebViewWnd->AddScriptToExecuteOnDocumentCreated(
              L"document.addEventListener('keydown', function(e) { if(e.keyCode == 32 && document.activeElement.type != \"text\") { IPlugSendMsg({\"msg\":\"keypress\",\"data\":e.keyCode}); }});",
              Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>([this](HRESULT error, PCWSTR id) -> HRESULT { return S_OK; }).Get());

            #ifndef _DEBUG
            // disables the right click on gui
            mWebViewWnd->AddScriptToExecuteOnDocumentCreated(
                L"document.addEventListener('contextmenu', event => event.preventDefault());document.addEventListener('contextmenu', event => event.preventDefault());",
                Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>
                  ([this](HRESULT error, PCWSTR id) -> HRESULT {
                    return S_OK;
                }).Get());
           #endif

                mWebViewWnd->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
                                                      wil::unique_cotaskmem_string jsonString;
                                                      args->get_WebMessageAsJson(&jsonString);
                                                      std::wstring jsonWString = jsonString.get();
                                                      WDL_String cStr;
                                                      UTF16ToUTF8(cStr, jsonWString.c_str());
                                                      OnMessageFromWebView(cStr.Get());
                                                      return S_OK;
                                                    }).Get(),
                                                    &mWebMessageReceivedToken);

                mWebViewWnd->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                                       BOOL success;
                                                       args->get_IsSuccess(&success);
                                                       if (success)
                                                       {
                                                         OnWebContentLoaded();
                                                       }
                                                       return S_OK;
                                                     }).Get(),
                                                     &mNavigationCompletedToken);

            mWebViewCtrlr->put_Bounds({ (LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h) });
            OnWebViewReady();
            return S_OK;
          }).Get());
      return S_OK;
    }).Get());

  return mParentWnd;
}

void IWebView::CloseWebView()
{
  if (mWebViewCtrlr.get() != nullptr)
  {
    mWebViewCtrlr->Close();
    mWebViewCtrlr = nullptr;
    mWebViewWnd = nullptr;
  }
}

void IWebView::HideWebView(bool hide)
{
  if (mWebViewCtrlr.get() != nullptr)
  {
    mWebViewCtrlr->put_IsVisible(!hide);
  }
  else
  {
    // the controller is set asynchonously, so we store the state 
    // to apply it when the controller is created
    mShowOnLoad = !hide;
  }
}

void IWebView::LoadHTML(const char* html)
{
  if (mWebViewWnd)
  {
    int size = strlen(html);
    WCHAR* htmlWide;
    htmlWide = new WCHAR[size]; // TODO: error check/size
    UTF8ToUTF16(htmlWide, html, size*2); // TODO: error check/size
    mWebViewWnd->NavigateToString(htmlWide);
    delete[] htmlWide;
  }
}

void IWebView::LoadURL(const char* url)
{
  //TODO: error check url?
  if (mWebViewWnd)
  {
    WCHAR urlWide[IPLUG_WIN_MAX_WIDE_PATH]; // TODO: error check/size
    UTF8ToUTF16(urlWide, url, IPLUG_WIN_MAX_WIDE_PATH); // TODO: error check/size
    mWebViewWnd->Navigate(urlWide);
  }
}

void IWebView::LoadFile(const char* fileName, const char* bundleID)
{
  if (mWebViewWnd)
  {
    WDL_String fullStr;
    fullStr.SetFormatted(MAX_WIN32_PATH_LEN, "file://%s", fileName);
    WCHAR fileUrlWide[IPLUG_WIN_MAX_WIDE_PATH]; // TODO: error check/size
    UTF8ToUTF16(fileUrlWide, fullStr.Get(), IPLUG_WIN_MAX_WIDE_PATH); // TODO: error check/size
    mWebViewWnd->Navigate(fileUrlWide);
  }
}

void IWebView::EvaluateJavaScript(const char* scriptStr, completionHandlerFunc func)
{
  if (mWebViewWnd)
  {
    WCHAR* scriptWide; // TODO: error check/size
    scriptWide = new WCHAR[1048576];

    UTF8ToUTF16(scriptWide, scriptStr, 1048576); // TODO: error check/size

    mWebViewWnd->ExecuteScript(scriptWide, Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
      [func](HRESULT errorCode, LPCWSTR resultObjectAsJson) -> HRESULT {
        if (func && resultObjectAsJson) {
          WDL_String str;
          UTF16ToUTF8(str, resultObjectAsJson);
          func(str.Get());
        }
        return S_OK;
      }).Get());
    delete [] scriptWide;
  }
}

void IWebView::EnableScroll(bool enable)
{
  /* NO-OP */
}

void IWebView::EnableInteraction(bool enable)
{
  /* NO-OP */
}

void IWebView::SetWebViewBounds(float x, float y, float w, float h, float scale)
{
  if (mWebViewCtrlr)
  {
    float ss = scale;
    x *= ss;
    y *= ss;
    w *= ss;
    h *= ss;

    mWebViewCtrlr->SetBoundsAndZoomFactor({ (LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h) }, scale);
  }
}
