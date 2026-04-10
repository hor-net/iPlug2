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

  // Check if editor dimensions are already scaled by comparing ratio to mScale
  // If GetEditorWidth() / previousWidth ≈ mScale, then dimensions are already scaled
  int editorWidth = GetEditorWidth();
  int editorHeight = GetEditorHeight();
  
  // Detect if already scaled: if ratio of consecutive opens ≈ mScale, it means host is giving scaled values
  static int sLastEditorWidth = 0;
  static int sLastEditorHeight = 0;
  
  float width, height;
  if (sLastEditorWidth > 0 && editorWidth > 0) {
    float widthRatio = (float)editorWidth / (float)sLastEditorWidth;
    float heightRatio = (float)editorHeight / (float)sLastEditorHeight;
    // If ratios are approximately equal to mScale, dimensions are already scaled
    if (fabs(widthRatio - mScale) < 0.1 && fabs(heightRatio - mScale) < 0.1) {
      // Already scaled by host, use unscaled values
      width = (float)editorWidth / mScale;
      height = (float)editorHeight / mScale;
    } else {
      // Not yet scaled, apply scale
      width = (float)editorWidth * mScale;
      height = (float)editorHeight * mScale;
    }
  } else {
    // First time, apply scale
    width = (float)editorWidth * mScale;
    height = (float)editorHeight * mScale;
  }
  
  sLastEditorWidth = (int)width;
  sLastEditorHeight = (int)height;

  if (mNeedsWindowRescale)
  {
    EditorResizeFromUI(width, height, true);
    mNeedsWindowRescale = false;
  }
    

  return OpenWebView(pParent, 0., 0., static_cast<float>(width), static_cast<float>(height), 1);
}

void WebViewEditorDelegate::Resize(int width, int height)
{
  // Note: dimensions from JavaScript are already in webview pixel space (scaled),
  // so we should NOT multiply by mScale again
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
