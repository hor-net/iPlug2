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

#if __has_feature(objc_arc)
#error This file must be compiled without Arc. Don't use -fobjc-arc flag!
#endif

#include "IPlugWebViewEditorDelegate.h"

#ifdef OS_MAC
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#elif defined(OS_IOS)
#import <UIKit/UIKit.h>
#endif

#if defined OS_MAC
  #define PLATFORM_VIEW NSView
#elif defined OS_IOS
  #define PLATFORM_VIEW UIView
#endif

using namespace iplug;

@interface IPLUG_WKWEBVIEW_EDITOR_HELPER : PLATFORM_VIEW
{
  WebViewEditorDelegate* mDelegate;
#ifdef OS_MAC
  NSView* mLoadingOverlay;
  NSProgressIndicator* mLoadingSpinner;
  NSTextField* mLoadingTitle;
  NSTextField* mLoadingMessage;
  NSScrollView* mLoadingDebugScroll;
  NSTextView* mLoadingDebugText;
  NSButton* mCopyDebugButton;
  NSTimer* mLoadingTimer;
  NSDate* mLoadingStartedAt;
  NSString* mLoadingDetails;
  NSString* mDebugInfo;
  NSInteger mLoadingState;
  NSInteger mTimeoutMs;
#endif
}
- (void) removeFromSuperview;
- (id) initWithEditorDelegate: (WebViewEditorDelegate*) pDelegate;
#ifdef OS_MAC
- (void) updateNativeLoadingState:(NSInteger)state timeout:(NSInteger)timeoutMs details:(const char*)details;
- (void) cancelLoadingTimer;
- (void) loadTimedOut:(NSTimer*)timer;
- (void) copyDebugInfo:(id)sender;
- (void) ensureLoadingOverlay;
#endif
@end

@implementation IPLUG_WKWEBVIEW_EDITOR_HELPER
{
}

- (id) initWithEditorDelegate: (WebViewEditorDelegate*) pDelegate;
{
  mDelegate = pDelegate;
  
#ifdef OS_IOS
  [[UIDevice currentDevice] beginGeneratingDeviceOrientationNotifications];
  [[NSNotificationCenter defaultCenter]
     addObserver:self selector:@selector(orientationChanged:)
     name:UIDeviceOrientationDidChangeNotification
     object:[UIDevice currentDevice]];
  
  CGRect r = [UIScreen mainScreen].bounds;
  CGFloat w = r.size.width;
  CGFloat h = r.size.height;
  
#else
  CGFloat w = pDelegate->GetEditorWidth();
  CGFloat h = pDelegate->GetEditorHeight();
  CGRect r = CGRectMake(0, 0, w, h);
#endif
  self = [super initWithFrame:r];
  void* pWebView = pDelegate->OpenWebView(self, 0, 0, w, h);
#ifdef OS_IOS
  [pWebView setAutoresizingMask: UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin | UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin];
#endif
  
  [self addSubview: (PLATFORM_VIEW*) pWebView];
#ifdef OS_MAC
  if (pDelegate->IsNativeLoadingOverlayEnabled())
    [self ensureLoadingOverlay];
#endif

  return self;
}

#ifdef OS_MAC
- (void)dealloc
{
  [self cancelLoadingTimer];
  [mLoadingOverlay release];
  [mLoadingSpinner release];
  [mLoadingTitle release];
  [mLoadingMessage release];
  [mLoadingDebugScroll release];
  [mLoadingDebugText release];
  [mCopyDebugButton release];
  [mLoadingStartedAt release];
  [mLoadingDetails release];
  [mDebugInfo release];
  [super dealloc];
}
#endif

- (void) removeFromSuperview
{
#ifdef OS_MAC
  [self updateNativeLoadingState:0 timeout:0 details:""];
#endif
#ifdef AU_API
  //For AUv2 this is where we know about the window being closed, close via delegate
  mDelegate->CloseWindow();
#endif
  
#ifdef OS_IOS
  [[NSNotificationCenter defaultCenter]
     removeObserver:self selector:@selector(orientationChanged:)
     name:UIDeviceOrientationDidChangeNotification
     object:[UIDevice currentDevice]];
#endif
  [super removeFromSuperview];
}

#ifdef OS_MAC
- (void)ensureLoadingOverlay
{
  if (mLoadingOverlay)
    return;

  mLoadingOverlay = [[NSView alloc] initWithFrame:self.bounds];
  [mLoadingOverlay setWantsLayer:YES];
  mLoadingOverlay.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.13 green:0.14 blue:0.16 alpha:1.0] CGColor];
  mLoadingOverlay.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  mLoadingSpinner = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
  mLoadingSpinner.style = NSProgressIndicatorStyleSpinning;
  mLoadingSpinner.controlSize = NSControlSizeRegular;
  mLoadingSpinner.displayedWhenStopped = NO;
  [mLoadingOverlay addSubview:mLoadingSpinner];

  mLoadingTitle = [[NSTextField alloc] initWithFrame:NSZeroRect];
  mLoadingTitle.bezeled = NO;
  mLoadingTitle.drawsBackground = NO;
  mLoadingTitle.editable = NO;
  mLoadingTitle.selectable = NO;
  mLoadingTitle.alignment = NSTextAlignmentCenter;
  mLoadingTitle.textColor = [NSColor whiteColor];
  mLoadingTitle.font = [NSFont systemFontOfSize:16 weight:NSFontWeightMedium];
  [mLoadingOverlay addSubview:mLoadingTitle];

  mLoadingMessage = [[NSTextField alloc] initWithFrame:NSZeroRect];
  mLoadingMessage.bezeled = NO;
  mLoadingMessage.drawsBackground = NO;
  mLoadingMessage.editable = NO;
  mLoadingMessage.selectable = NO;
  mLoadingMessage.alignment = NSTextAlignmentCenter;
  mLoadingMessage.textColor = [NSColor colorWithCalibratedWhite:0.78 alpha:1.0];
  mLoadingMessage.font = [NSFont systemFontOfSize:12];
  mLoadingMessage.cell.wraps = YES;
  [mLoadingOverlay addSubview:mLoadingMessage];

  mLoadingDebugScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  mLoadingDebugScroll.hasVerticalScroller = YES;
  mLoadingDebugScroll.borderType = NSBezelBorder;
  mLoadingDebugText = [[NSTextView alloc] initWithFrame:NSZeroRect];
  mLoadingDebugText.editable = NO;
  mLoadingDebugText.selectable = YES;
  mLoadingDebugText.font = [NSFont userFixedPitchFontOfSize:10.0];
  mLoadingDebugText.textColor = [NSColor colorWithCalibratedWhite:0.88 alpha:1.0];
  mLoadingDebugText.backgroundColor = [NSColor colorWithCalibratedRed:0.10 green:0.11 blue:0.12 alpha:1.0];
  mLoadingDebugScroll.drawsBackground = YES;
  mLoadingDebugScroll.backgroundColor = mLoadingDebugText.backgroundColor;
  mLoadingDebugScroll.documentView = mLoadingDebugText;
  [mLoadingOverlay addSubview:mLoadingDebugScroll];

  mCopyDebugButton = [[NSButton alloc] initWithFrame:NSZeroRect];
  mCopyDebugButton.title = @"Copy debug info";
  mCopyDebugButton.bezelStyle = NSBezelStyleRounded;
  mCopyDebugButton.target = self;
  mCopyDebugButton.action = @selector(copyDebugInfo:);
  [mLoadingOverlay addSubview:mCopyDebugButton];

  mLoadingOverlay.hidden = YES;
  [self addSubview:mLoadingOverlay positioned:NSWindowAbove relativeTo:nil];
  [self setNeedsLayout:YES];
}

- (void)layout
{
  [super layout];
  if (!mLoadingOverlay)
    return;

  const NSRect bounds = self.bounds;
  mLoadingOverlay.frame = bounds;
  const CGFloat contentWidth = MIN(680.0, MAX(300.0, bounds.size.width - 64.0));
  const CGFloat centerX = NSMidX(bounds);
  const CGFloat centerY = NSMidY(bounds);

  if (mLoadingState == 1)
  {
    mLoadingSpinner.hidden = NO;
    mLoadingTitle.hidden = NO;
    mLoadingMessage.hidden = YES;
    mLoadingDebugScroll.hidden = YES;
    mCopyDebugButton.hidden = YES;
    mLoadingSpinner.frame = NSMakeRect(centerX - 12.0, centerY + 8.0, 24.0, 24.0);
    mLoadingTitle.frame = NSMakeRect(centerX - contentWidth * 0.5, centerY - 32.0, contentWidth, 26.0);
  }
  else
  {
    mLoadingSpinner.hidden = YES;
    mLoadingTitle.hidden = NO;
    mLoadingMessage.hidden = NO;
    mLoadingDebugScroll.hidden = NO;
    mCopyDebugButton.hidden = NO;
    mLoadingTitle.frame = NSMakeRect(centerX - contentWidth * 0.5, bounds.size.height - 78.0, contentWidth, 28.0);
    mLoadingMessage.frame = NSMakeRect(centerX - contentWidth * 0.5, bounds.size.height - 126.0, contentWidth, 42.0);
    mLoadingDebugScroll.frame = NSMakeRect(centerX - contentWidth * 0.5, 72.0, contentWidth, MAX(90.0, bounds.size.height - 230.0));
    mLoadingDebugText.frame = NSMakeRect(0.0, 0.0, contentWidth - 20.0, MAX(90.0, bounds.size.height - 230.0));
    mCopyDebugButton.frame = NSMakeRect(centerX - 70.0, 28.0, 140.0, 32.0);
  }
}

- (void)cancelLoadingTimer
{
  if (mLoadingTimer)
  {
    [mLoadingTimer invalidate];
    [mLoadingTimer release];
    mLoadingTimer = nil;
  }
}

- (void)updateNativeLoadingState:(NSInteger)state timeout:(NSInteger)timeoutMs details:(const char*)details
{
  if (state == 0)
  {
    [self cancelLoadingTimer];
    mLoadingState = 0;
    [mLoadingSpinner stopAnimation:nil];
    mLoadingSpinner.hidden = YES;
    mLoadingOverlay.hidden = YES;
    return;
  }
  // Lifecycle progress updates must not restart the original 5-second timer
  // or replace a timeout already shown to the user.
  if (state == 3)
    return;

  [self ensureLoadingOverlay];
  [self cancelLoadingTimer];
  mLoadingState = state;
  mTimeoutMs = MAX(100, timeoutMs);
  [mLoadingDetails release];
  mLoadingDetails = [[NSString alloc] initWithUTF8String:(details ? details : "")];
  mLoadingOverlay.hidden = NO;

  if (state == 1)
  {
    mLoadingTitle.stringValue = @"Loading UI…";
    mLoadingMessage.hidden = YES;
    mLoadingDebugScroll.hidden = YES;
    mCopyDebugButton.hidden = YES;
    [mLoadingSpinner startAnimation:nil];
    [mLoadingStartedAt release];
    mLoadingStartedAt = [[NSDate date] retain];
    mLoadingTimer = [[NSTimer timerWithTimeInterval:(double)mTimeoutMs / 1000.0
      target:self selector:@selector(loadTimedOut:) userInfo:nil repeats:NO] retain];
    [[NSRunLoop mainRunLoop] addTimer:mLoadingTimer forMode:NSRunLoopCommonModes];
  }
  else
  {
    [self cancelLoadingTimer];
    [mLoadingSpinner stopAnimation:nil];
    mLoadingTitle.stringValue = @"UI failed to load";
    mLoadingMessage.stringValue = mLoadingDetails;
    const std::string delegateInfo = mDelegate->GetNativeLoadingDiagnostics();
    const NSRect webFrame = ((NSView*)[self.subviews firstObject]).frame;
    NSString* hostName = [[NSProcessInfo processInfo] processName];
    NSString* osVersion = [[NSProcessInfo processInfo] operatingSystemVersionString];
    [mDebugInfo release];
    mDebugInfo = [[NSString alloc] initWithFormat:
      @"Reason: %@\nHost: %@ (PID %d)\nmacOS: %@\nEditor attached to window: %@\nWebView frame: %.0f x %.0f\n%@",
      mLoadingDetails, hostName, (int)[[NSProcessInfo processInfo] processIdentifier], osVersion, self.window ? @"yes" : @"no",
      webFrame.size.width, webFrame.size.height,
      [NSString stringWithUTF8String:delegateInfo.c_str()]];
    mLoadingDebugText.string = mDebugInfo;
  }

  [self setNeedsLayout:YES];
  [self layoutSubtreeIfNeeded];
}

- (void)loadTimedOut:(NSTimer*)timer
{
  if (timer != mLoadingTimer || mLoadingState != 1)
    return;
  [mLoadingTimer release];
  mLoadingTimer = nil;
  TraceWebView(mDelegate, "native-loading-timeout", "timeout-ms=%ld", (long)mTimeoutMs);
  NSString* reason = [NSString stringWithFormat:
    @"JavaScript readiness (JSREADY) was not received within %.1f seconds.", (double)mTimeoutMs / 1000.0];
  [self updateNativeLoadingState:2 timeout:mTimeoutMs details:[reason UTF8String]];
}

- (void)copyDebugInfo:(id)sender
{
  (void)sender;
  if (!mDebugInfo)
    return;
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  [pasteboard setString:mDebugInfo forType:NSPasteboardTypeString];
}
#endif

#ifdef OS_IOS
- (void) orientationChanged:(NSNotification *)note
{
  
  CGRect r = self.bounds; //[UIScreen mainScreen].bounds;
  CGFloat w = r.size.width;
  CGFloat h = r.size.height;
  
   UIDevice * device = note.object;
   switch(device.orientation)
   {
     case UIDeviceOrientationPortrait:
     case UIDeviceOrientationPortraitUpsideDown:
       w = std::min(r.size.width, r.size.height);
       h = std::max(r.size.width, r.size.height);
       break;
     
     
       break;
       
     case UIDeviceOrientationLandscapeLeft:
     case UIDeviceOrientationLandscapeRight:
       w = std::max(r.size.width, r.size.height);
       h = std::min(r.size.width, r.size.height);
       break;

     default:
       break;
   };
  
  UIEdgeInsets safeAreaInsets = self.safeAreaInsets;
  w = w - safeAreaInsets.top - safeAreaInsets.bottom;
  h = h - safeAreaInsets.left - safeAreaInsets.right;
  
  mDelegate->Resize(w,h);
}
#endif

@end

#ifdef OS_MAC
namespace iplug {
void UpdateNativeWebViewLoadingOverlay(void* pView, int state, int timeoutMs, const char* details)
{
  if (!pView)
    return;
  [(IPLUG_WKWEBVIEW_EDITOR_HELPER*)pView updateNativeLoadingState:state timeout:timeoutMs details:details];
}
}
#endif

WebViewEditorDelegate::WebViewEditorDelegate(int nParams)
: IEditorDelegate(nParams)
#if defined _DEBUG
, IWebView(true, true)
#else
, IWebView(true, false)
#endif
, mWebViewReady(false)
{
  
}

WebViewEditorDelegate::~WebViewEditorDelegate()
{
  CloseWindow();
  
  PLATFORM_VIEW* pHelperView = (PLATFORM_VIEW*) mView;
  [pHelperView release];
  mView = nullptr;
}

void* WebViewEditorDelegate::OpenWindow(void* pParent)
{
  PLATFORM_VIEW* pParentView = (PLATFORM_VIEW*) pParent;
  TraceWebView(static_cast<IWebView*>(this), "editor-open", "parent=%p window=%p",
    pParent, (void*)pParentView.window);
    
  IPLUG_WKWEBVIEW_EDITOR_HELPER* pHelperView = [[IPLUG_WKWEBVIEW_EDITOR_HELPER alloc] initWithEditorDelegate: this];
  mView = (void*) pHelperView;

  if (pParentView)
  {
    [pParentView addSubview: pHelperView];
  }
  
  [pHelperView setFrame:CGRectMake(0, 0, GetEditorWidth(), GetEditorHeight())];
  SetWebViewBounds(0, 0, GetEditorWidth(), GetEditorHeight());
  
  // The view is now created AND attached to its parent window: start the page
  // load if the webview is ready (order-independent, exactly once).
  mEditorViewAttached = true;
  TraceWebView(static_cast<IWebView*>(this), "editor-attach", "window=%p", (void*)pHelperView.window);
  TryStartEditorInit();

  return mView;
}

void WebViewEditorDelegate::Resize(int width, int height)
{
  ResizeWebViewAndHelper(width, height);
  EditorResizeFromUI(width, height, true);
}

void WebViewEditorDelegate::OnParentWindowResize(int width, int height)
{
  ResizeWebViewAndHelper(width, height);
  EditorResizeFromUI(width, height, false);
}

void WebViewEditorDelegate::ResizeWebViewAndHelper(float width, float height)
{
  CGFloat w = static_cast<float>(width);
  CGFloat h = static_cast<float>(height);
  IPLUG_WKWEBVIEW_EDITOR_HELPER* pHelperView = (IPLUG_WKWEBVIEW_EDITOR_HELPER*) mView;
  [pHelperView setFrame:CGRectMake(0, 0, w, h)];
  SetWebViewBounds(0, 0, w, h);
}

bool WebViewEditorDelegate::OnKeyDown(const IKeyPress& key)
{
  return false;
}

bool WebViewEditorDelegate::OnKeyUp(const IKeyPress& key)
{
  return false;
}

// JavaScript message queue system implementation
#ifdef OS_MAC
void WebViewEditorDelegate::QueueJavaScript(const char* scriptStr)
{
  std::lock_guard<std::mutex> lock(mQueueMutex);
  
  //printf("QueueJavaScript called: %s (ready: %s)\n", scriptStr, mWebViewReady ? "YES" : "NO");
  
  if (mWebViewReady)
  {
    //printf("EXECUTING IMMEDIATELY: %s\n", scriptStr);
    EvaluateJavaScript(scriptStr);
  }
  else
  {
    //printf("ADDING TO QUEUE: %s\n", scriptStr);
    mJavaScriptQueue.push(std::string(scriptStr));
  }
}

void WebViewEditorDelegate::FlushJavaScriptQueue()
{
  std::lock_guard<std::mutex> lock(mQueueMutex);
  
  //printf("FlushJavaScriptQueue called, queue size: %zu\n", mJavaScriptQueue.size());
  
  while (!mJavaScriptQueue.empty())
  {
    const std::string& script = mJavaScriptQueue.front();
    //printf("Flushing from queue: %s\n", script.c_str());
    EvaluateJavaScript(script.c_str());
    mJavaScriptQueue.pop();
  }
}
#endif

