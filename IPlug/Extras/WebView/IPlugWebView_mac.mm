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

#include <objc/objc.h>
#if !__has_feature(objc_arc)
#error This file must be compiled with Arc. Use -fobjc-arc flag
#endif

#import <WebKit/WebKit.h>
#import "IPlugWKWebView.h"
#import "IPlugWKWebViewScriptMessageHandler.h"
#import "IPlugWKWebViewDelegate.h"
#import "IPlugWKWebViewUIDelegate.h"

#include "IPlugWebView.h"
#include "IPlugWebViewDiagnostics.h"
#include "IPlugPaths.h"

namespace iplug {
extern bool GetResourcePathFromBundle(const char* fileName, const char* searchExt, WDL_String& fullPath, const char* bundleID);
}

BEGIN_IPLUG_NAMESPACE

class IWebViewImpl
{
public:
  IWebViewImpl(IWebView* owner);
  ~IWebViewImpl();
  
  void* OpenWebView(void* pParent, float x, float y, float w, float h, float scale);
  void CloseWebView();
  void HideWebView(bool hide);
  
  void LoadHTML(const char* html);
  void LoadURL(const char* url);
  void LoadFile(const char* fileName, const char* _Nullable bundleID);
  void ReloadPageContent();
  void EvaluateJavaScript(const char* scriptStr, IWebView::completionHandlerFunc func);
  void EnableScroll(bool enable);
  void EnableInteraction(bool enable);
  void SetWebViewBounds(float x, float y, float w, float h, float scale);
  void GetWebRoot(WDL_String& path) const { path.Set(mWebRoot.Get()); }

  void GetLocalDownloadPathForFile(const char* fileName, WDL_String& localPath);

private:
  IWebView* mIWebView;
  WDL_String mWebRoot;
  WKWebViewConfiguration* _Nullable mWebConfig;
  IPLUG_WKWEBVIEW* _Nullable mWKWebView;
  IPLUG_WKSCRIPTMESSAGEHANDLER* _Nullable mScriptMessageHandler;
  IPLUG_WKWEBVIEW_DELEGATE* _Nullable mNavigationDelegate;
  IPLUG_WKWEBVIEW_UI_DELEGATE* _Nullable mUIDelegate;
};

END_IPLUG_NAMESPACE

using namespace iplug;

#pragma mark - Impl

IWebViewImpl::IWebViewImpl(IWebView* owner)
: mIWebView(owner)
, mWebConfig(nil)
, mWKWebView(nil)
, mScriptMessageHandler(nil)
, mNavigationDelegate(nil)
{
}

IWebViewImpl::~IWebViewImpl()
{
  CloseWebView();
}

void* IWebViewImpl::OpenWebView(void* pParent, float x, float y, float w, float h, float scale)
{
  TraceWebView(mIWebView, "create", "parent=%p size=%.0fx%.0f", pParent, w, h);
  WKWebViewConfiguration* webConfig = [[WKWebViewConfiguration alloc] init];
  WKPreferences* preferences = [[WKPreferences alloc] init];
  
  WKUserContentController* controller = [[WKUserContentController alloc] init];
  webConfig.userContentController = controller;

  [webConfig setValue:@YES forKey:@"allowUniversalAccessFromFileURLs"];
  auto* scriptMessageHandler = [[IPLUG_WKSCRIPTMESSAGEHANDLER alloc] initWithIWebView: mIWebView];
  [controller addScriptMessageHandler: scriptMessageHandler name:@"callback"];

  if (mIWebView->GetEnableDevTools())
  {
    [preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    
  }
  
  [preferences setValue:@YES forKey:@"DOMPasteAllowed"];
  [preferences setValue:@YES forKey:@"javaScriptCanAccessClipboard"];
  
  webConfig.preferences = preferences;
  if (@available(macOS 10.13, *))
  {
    NSString* customUrlScheme = [NSString stringWithUTF8String:mIWebView->GetCustomUrlScheme()];
    const BOOL useCustomUrlScheme = [customUrlScheme length];

    if (useCustomUrlScheme)
    {
      [webConfig setURLSchemeHandler:scriptMessageHandler forURLScheme:[NSString stringWithUTF8String:mIWebView->GetCustomUrlScheme()]];
    }
  }
  
  // this script adds a function IPlugSendMsg that is used to call the platform webview messaging function in JS
  [controller addUserScript:[[WKUserScript alloc] initWithSource:
                             @"function IPlugSendMsg(m) { webkit.messageHandlers.callback.postMessage(m); }"
                             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                             forMainFrameOnly:YES]];

#ifdef _DEBUG
  // Sparse diagnostic events, not message payloads: distinguish script errors
  // from the independent 2D-grid and WebGL-spectrum context lifecycles.
  [controller addUserScript:[[WKUserScript alloc] initWithSource:
    @"(function(){function report(event,line,column){try{IPlugSendMsg({msg:'WEBVIEW_DIAGNOSTIC',event:event,line:line||0,column:column||0});}catch(e){}} window.addEventListener('error',function(e){report('page-error',e.lineno,e.colno);}); ['contextlost','contextrestored','webglcontextlost','webglcontextrestored'].forEach(function(type){window.addEventListener(type,function(e){var chart=document.chart;if(!chart)return;var grid=e.target===chart._gridCanvas;var spectrum=e.target===chart._spectrumCanvas;if(grid||spectrum)report((grid?'grid':'spectrum')+'-context-'+(type.indexOf('restored')>=0?'restored':'lost'));},true);});})();"
    injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
#endif

  // this script prevents view scaling on iOS
  [controller addUserScript:[[WKUserScript alloc] initWithSource:
                             @"var meta = document.createElement('meta'); meta.name = 'viewport'; \
                               meta.content = 'width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, shrink-to-fit=YES'; \
                               var head = document.getElementsByTagName('head')[0]; \
                               head.appendChild(meta);"
                             injectionTime:WKUserScriptInjectionTimeAtDocumentEnd
                             forMainFrameOnly:YES]];
  
  // this script waits for DOMContentLoaded and then notifies C++ that JavaScript is ready
  // The notification is RETRIED until the C++ side has processed it: a single
  // JSREADY could be lost (script-handler lifetime races during editor
  // close/reopen, IPlugSendMsg not yet defined, dropped message) and the
  // editor would stay white and unresponsive forever. OnWebContentLoaded is
  // idempotent so the extra notifications are harmless.
  [controller addUserScript:[[WKUserScript alloc] initWithSource:
                             @"(function() { \
                                var tries = 0, started = false; \
                                window.IPlugDocumentId = Date.now().toString(36) + Math.random().toString(36).slice(2); \
                                function ping() { \
                                  if (window.IPlugJsAck) return; \
                                  try { IPlugSendMsg({'msg': 'JSREADY', 'documentId': window.IPlugDocumentId}); } catch (e) {} \
                                  if (++tries < 20) setTimeout(ping, 500); \
                                } \
                                function start() { if (started) return; started = true; ping(); } \
                                window.addEventListener('load', start); \
                                document.addEventListener('DOMContentLoaded', start); \
                              })();"
                             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                             forMainFrameOnly:YES]];
  
  // this script receives global key down events and forwards them to the C++ side
//  [controller addUserScript:[[WKUserScript alloc] initWithSource:
//                             @"document.addEventListener('keydown', function(e) { if(document.activeElement.type != \"text\" || e.code != 'Space' ) { IPlugSendMsg({'msg': 'SKPFUI', 'keyCode': e.keyCode, 'utf8': e.key, 'S': e.shiftKey, 'C': e.ctrlKey, 'A': e.altKey, 'isUp': false}); e.preventDefault(); }});"
//                             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
//                             forMainFrameOnly:YES]];
//  
//  // this script receives global key up events and forwards them to the C++ side
//  [controller addUserScript:[[WKUserScript alloc] initWithSource:
//                             @"document.addEventListener('keyup', function(e) { if(document.activeElement.type != \"text\" || e.code != 'Space' ) { IPlugSendMsg({'msg': 'SKPFUI', 'keyCode': e.keyCode, 'utf8': e.key, 'S': e.shiftKey, 'C': e.ctrlKey, 'A': e.altKey, 'isUp': true}); e.preventDefault(); }});"
//                             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
//                             forMainFrameOnly:YES]];
//  
  IPLUG_WKWEBVIEW* wkWebView = [[IPLUG_WKWEBVIEW alloc] initWithFrame: CGRectMake(x, y, w, h) configuration:webConfig];
  
  const auto isTransparent = !mIWebView->IsOpaque();

#if defined OS_IOS
  if (isTransparent)
  {
    wkWebView.backgroundColor = [UIColor clearColor];
    wkWebView.scrollView.backgroundColor = [UIColor clearColor];
    wkWebView.opaque = NO;
  }
  
  // Aggiungi questa configurazione per l'ispezione iOS
  if (mIWebView->GetEnableDevTools())
  {
    if (@available(iOS 16.4, *)) {
      wkWebView.inspectable = YES;
    }
  }
#endif

#if defined OS_MAC
  if (isTransparent)
  {
    [wkWebView setValue:@(NO) forKey:@"drawsBackground"];
  }
  
  [wkWebView setAllowsMagnification:NO];
#endif
  
  auto* navigationDelegate = [[IPLUG_WKWEBVIEW_DELEGATE alloc] initWithIWebView: mIWebView];
  [wkWebView setNavigationDelegate:navigationDelegate];

  auto* uiDelegate = [[IPLUG_WKWEBVIEW_UI_DELEGATE alloc] initWithIWebView: mIWebView];
  [wkWebView setUIDelegate:uiDelegate];

  mWebConfig = webConfig;
  mWKWebView = wkWebView;
  mScriptMessageHandler = scriptMessageHandler;
  mNavigationDelegate = navigationDelegate;
  mUIDelegate = uiDelegate;
  
  mIWebView->OnWebViewReady();

  return (__bridge void*) wkWebView;
}

void IWebViewImpl::CloseWebView()
{
  // IMPORTANT: this can run while the WKWebView is being torn down
  // out-of-order (e.g. ~IWebView destroys the IWebViewImpl when the
  // editor is already half-detached) or be called twice (once from the
  // editor's CloseWindow, once from ~IWebView via IWebView's destructor).
  // Guard every Objective-C message against nil and against a double call
  // so we don't crash REAPER's main thread with a use-after-free in
  // WebKit::AuxiliaryProcessProxy::sendMessage -> ProcessThrottler.
  if (mWKWebView == nil) {
    // Already closed (or never opened). Just keep the ivars consistent so
    // the EvaluateJavaScript() guard `if (mWKWebView && ...)` is still a
    // reliable short-circuit on the next call.
    mWebConfig = nil;
    mScriptMessageHandler = nil;
    mNavigationDelegate = nil;
    return;
  }

  TraceWebView(mIWebView, "close", "view=%p", (__bridge void*)mWKWebView);
  // Pending navigation from a closed editor must not alter the readiness of
  // the next editor (or call back into its destroyed C++ owner).
  mWKWebView.navigationDelegate = nil;
  mWKWebView.UIDelegate = nil;
  [mWKWebView stopLoading];
  // Break the WKUserContentController -> script handler retain cycle.
  // Without this, the controller keeps the handler alive across editor
  // close/reopen cycles, so a stale WKScriptMessageHandler can post a
  // message back into an IWebView that has been destroyed. We only call
  // this if the configuration still owns a controller (LoadHTML/LoadFile
  // paths set mWebConfig; programmatic-only paths may not).
  if (mWebConfig && mWebConfig.userContentController) {
    [mWebConfig.userContentController removeScriptMessageHandlerForName:@"callback"];
  }

  [mWKWebView removeFromSuperview];

  mWebConfig = nil;
  mWKWebView = nil;
  mScriptMessageHandler = nil;
  mNavigationDelegate = nil;
}

void IWebViewImpl::HideWebView(bool hide)
{
  mWKWebView.hidden = hide;
}

void IWebViewImpl::LoadHTML(const char* html)
{
  TraceWebView(mIWebView, "load-html", "view=%p bytes=%zu", (__bridge void*)mWKWebView, strlen(html));
  [mWKWebView loadHTMLString:[NSString stringWithUTF8String:html] baseURL:nil];
}

void IWebViewImpl::LoadURL(const char* url)
{
  NSURL* nsURL = [NSURL URLWithString:[NSString stringWithUTF8String:url] relativeToURL:nil];
  NSURLRequest* req = [[NSURLRequest alloc] initWithURL:nsURL];
  [mWKWebView loadRequest:req];
}

void IWebViewImpl::LoadFile(const char* fileName, const char* _Nullable bundleID)
{
  WDL_String fullPath;
  
  if (bundleID != nullptr && strlen(bundleID) != 0)
  {
    WDL_String fileNameWeb("web/");
    fileNameWeb.Append(fileName);
    
    GetResourcePathFromBundle(fileNameWeb.Get(), fileNameWeb.get_fileext() + 1 /* remove . */, fullPath, bundleID);
  }
  else
  {
    fullPath.Set(fileName);
  }
  
  NSString* pPath = [NSString stringWithUTF8String:fullPath.Get()];
  
  fullPath.remove_filepart();
  mWebRoot.Set(fullPath.Get());

  // If a custom url scheme is provided use it, otherwise use a file Url
  NSString* customUrlScheme = [NSString stringWithUTF8String:mIWebView->GetCustomUrlScheme()];
  const BOOL useCustomUrlScheme = [customUrlScheme length];
  NSString* urlScheme = @"file:";
  
  if (useCustomUrlScheme)
  {
    urlScheme = [urlScheme stringByReplacingOccurrencesOfString:@"file" withString:customUrlScheme];
  }
  
  NSString* webroot = [urlScheme stringByAppendingString:[pPath stringByReplacingOccurrencesOfString:[NSString stringWithUTF8String:fileName] withString:@""]];

  NSURL* pageUrl = [NSURL URLWithString:[webroot stringByAppendingString:[NSString stringWithUTF8String:fileName]] relativeToURL:nil];

  if (useCustomUrlScheme)
  {
#if defined OS_MAC && defined _DEBUG
    NSString* homeDir = NSHomeDirectory();
    
    if ([homeDir containsString:@"Library/Containers/"])
    {
      NSString* absolutePath = [[pageUrl path] stringByStandardizingPath];
      if (![absolutePath hasPrefix:homeDir]) {
        NSLog(@"Warning: Attempting to load URL outside container directory in sandboxed app: %@", absolutePath);
      }
    }
#endif
    
    NSURLRequest* req = [[NSURLRequest alloc] initWithURL:pageUrl];
    [mWKWebView loadRequest:req];
  }
  else
  {
    NSURL* rootUrl = [NSURL URLWithString:webroot relativeToURL:nil];
    TraceWebView(mIWebView, "load-file", "view=%p page-valid=%d root-valid=%d root-path-length=%lu",
      (__bridge void*)mWKWebView, pageUrl != nil, rootUrl != nil, (unsigned long)rootUrl.path.length);
    [mWKWebView loadFileURL:pageUrl allowingReadAccessToURL:rootUrl];
  }
}

void IWebViewImpl::ReloadPageContent()
{
  [mWKWebView reload];
}

void IWebViewImpl::EvaluateJavaScript(const char* scriptStr, IWebView::completionHandlerFunc func)
{
  // DOM readiness is managed by the editor's JSREADY handshake. WebKit can
  // still report loading after DOMContentLoaded: dropping evaluations here
  // loses the entire initial parameter snapshot without reporting an error.
  if (mWKWebView)
  {
    const void* pTraceOwner = mIWebView; // identity only; callback must not dereference a closed owner
    [mWKWebView evaluateJavaScript:[NSString stringWithUTF8String:scriptStr] completionHandler:^(NSString *result, NSError *error) {
      if (error != nil)
      {
        TraceWebView(pTraceOwner, "js-error", "domain=%s code=%ld", error.domain.UTF8String, (long)error.code);
        NSLog(@"Error %@",error);
      }
      else if(func)
      {
        func([result UTF8String]);
      }
    }];
  }
}

void IWebViewImpl::EnableScroll(bool enable)
{
#ifdef OS_IOS
  [mWKWebView.scrollView setScrollEnabled:enable];
#endif
}

void IWebViewImpl::EnableInteraction(bool enable)
{
  [mWKWebView setEnableInteraction:enable];
}

void IWebViewImpl::SetWebViewBounds(float x, float y, float w, float h, float scale)
{
  [mWKWebView setFrame: CGRectMake(x, y, w, h) ];

#ifdef OS_MAC
  if (@available(macOS 11.0, *)) {
    [mWKWebView setPageZoom:scale ];
  }
#endif
}

void IWebViewImpl::GetLocalDownloadPathForFile(const char* fileName, WDL_String& localPath)
{
  NSURL* url = [[NSURL fileURLWithPath:NSTemporaryDirectory()] URLByAppendingPathComponent:[[NSUUID UUID] UUIDString] isDirectory:YES];
  NSError *error = nil;
  @try {
    [[NSFileManager defaultManager] createDirectoryAtURL:url withIntermediateDirectories:YES attributes:nil error:&error];
    url = [url URLByAppendingPathComponent:[NSString stringWithUTF8String:fileName]];
    localPath.Set([[url absoluteString] UTF8String]);
  } @catch (NSException *exception)
  {
    NSLog(@"Error %@",error);
  }
}

#include "IPlugWebView.cpp"

#include "IPlugWKWebView.mm"
#include "IPlugWKWebViewScriptMessageHandler.mm"
#include "IPlugWKWebViewDelegate.mm"
#include "IPlugWKWebViewUIDelegate.mm"
