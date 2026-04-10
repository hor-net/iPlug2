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

#include "IPlugWebViewEditorDelegate.h"
#include "winuser.h"

extern float GetScaleForHWND(HWND hWnd);

using namespace iplug;

WebViewEditorDelegate::WebViewEditorDelegate(int nParams)
  : IEditorDelegate(nParams)
  , IWebView()
{
}

WebViewEditorDelegate::~WebViewEditorDelegate()
{
  CloseWindow();
}

void* WebViewEditorDelegate::OpenWindow(void* pParent)
{
  mScale = GetScaleForHWND((HWND)pParent);
  int editorW = GetEditorWidth();
  int editorH = GetEditorHeight();

  float width = (float)editorW * mScale;
  float height = (float)editorH * mScale;

  // DEBUG: Log zoom tracking
  char buf[512];
  sprintf(buf, "[iPlug] OpenWindow - editor: %dx%d, mScale: %.2f, final: %.0fx%.0f\n", editorW, editorH, mScale, width, height);
  OutputDebugStringA(buf);

  if (mNeedsWindowRescale)
  {
    EditorResizeFromUI(width, height, true);
    mNeedsWindowRescale = false;
  }
    

  return OpenWebView(pParent, 0., 0., static_cast<float>(width), static_cast<float>(height), 1);
}

void WebViewEditorDelegate::Resize(int width, int height)
{
  // DEBUG
  char buf[256];
  sprintf(buf, "[iPlug] Resize - input: %dx%d, mScale: %.2f\n", width, height, mScale);
  OutputDebugStringA(buf);
  
  width *= mScale;
  height *= mScale;
  sprintf(buf, "[iPlug] Resize - after scale: %dx%d\n", width, height);
  OutputDebugStringA(buf);
  
  SetWebViewBounds(0, 0, static_cast<float>(width), static_cast<float>(height), 1);
  EditorResizeFromUI(width, height, true);
}

void WebViewEditorDelegate::OnParentWindowResize(int width, int height)
{
  SetWebViewBounds(0, 0, static_cast<float>(width), static_cast<float>(height), 1);
  EditorResizeFromUI(width, height, false);
}

bool WebViewEditorDelegate::OnKeyDown(const IKeyPress& key)
{
  #ifdef OS_WIN
  if (key.VK == VK_SPACE)
  {
    PostMessage((HWND)mView, WM_KEYDOWN, VK_SPACE, 0);
    return true;
  }
  #endif
  return false;
}

bool WebViewEditorDelegate::OnKeyUp(const IKeyPress& key)
{

  return true;
}
