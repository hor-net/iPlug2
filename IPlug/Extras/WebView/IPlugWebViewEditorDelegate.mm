 /*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#if __has_feature(objc_arc)
#error This file must be compiled without Arc. Don't use -fobjc-arc flag!
#endif

#include "IPlugWebViewEditorDelegate.h"

#ifdef OS_IOS
#import <UIKit/UIKit.h>
#endif

using namespace iplug;

@interface HELPER_VIEW : PLATFORM_VIEW
{
  WebViewEditorDelegate* mDelegate;
}
- (void) removeFromSuperview;
- (id) initWithEditorDelegate: (WebViewEditorDelegate*) pDelegate;
@end

@implementation HELPER_VIEW
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
  
  CGRect r = [UIScreen mainScreen].applicationFrame;
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
  [pWebView setAutoresizingMask:UIViewAutoresizingFlexibleHeight | UIViewAutoresizingFlexibleWidth];
#endif
  
  [self addSubview: (PLATFORM_VIEW*) pWebView];

  return self;
}

- (void) removeFromSuperview
{
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

#ifdef OS_IOS
- (void) orientationChanged:(NSNotification *)note
{
  
  CGRect r = [UIScreen mainScreen].applicationFrame;
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
  
  mDelegate->Resize(w,h);
}
#endif

@end

WebViewEditorDelegate::WebViewEditorDelegate(int nParams)
: IEditorDelegate(nParams)
, IWebView()
{
  
}

WebViewEditorDelegate::~WebViewEditorDelegate()
{
  CloseWindow();
  
  PLATFORM_VIEW* pHelperView = (PLATFORM_VIEW*) mHelperView;
  [pHelperView release];
  mHelperView = nullptr;
}

void* WebViewEditorDelegate::OpenWindow(void* pParent)
{
  PLATFORM_VIEW* pParentView = (PLATFORM_VIEW*) pParent;
  
  HELPER_VIEW* pHelperView;
  if(mHelperView == nullptr) {
    pHelperView = [[HELPER_VIEW alloc] initWithEditorDelegate: this];
    mHelperView = (void*) pHelperView;
  } else {
    pHelperView = (HELPER_VIEW*)mHelperView;
  }
  

  if (pParentView) {
    [pParentView addSubview: pHelperView];
  }
  
  //Resize(GetEditorWidth(), GetEditorHeight());
  [pHelperView setFrame:CGRectMake(0, 0, GetEditorWidth(), GetEditorHeight())];
  SetWebViewBounds(0, 0, GetEditorWidth(), GetEditorHeight());
  
  if (mEditorInitFunc)
    mEditorInitFunc();

  return mHelperView;
}

void WebViewEditorDelegate::CloseWindow()
{
#ifndef AU_API
  HELPER_VIEW* pHelperView = (HELPER_VIEW*)mHelperView;
  [pHelperView removeFromSuperview];
#endif
}

void WebViewEditorDelegate::Resize(int width, int height)
{
  CGFloat w = static_cast<float>(width);
  CGFloat h = static_cast<float>(height);
  HELPER_VIEW* pHelperView = (HELPER_VIEW*) mHelperView;
  [pHelperView setFrame:CGRectMake(0, 0, w, h)];
  SetWebViewBounds(0, 0, w, h);
  EditorResizeFromUI(width, height, true);
}
