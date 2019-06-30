#include <cmath>

#include "IGraphicsSkia.h"

#include "SkDashPathEffect.h"
#include "SkGradientShader.h"
#include "SkFont.h"
#include "SkFontMetrics.h"

#include "gl/GrGLInterface.h"
#include "gl/GrGLUtil.h"
#include "GrContext.h"

#ifdef OS_MAC
#include "SkCGUtils.h"
#endif

#include <OpenGL/gl.h>


SkiaBitmap::SkiaBitmap(GrContext* context, int width, int height, int scale, float drawScale)
{
#ifdef IGRAPHICS_GL
  SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
  mDrawable.mSurface = SkSurface::MakeRenderTarget(context, SkBudgeted::kYes, info);
#else
  mDrawable.mSurface = SkSurface::MakeRasterN32Premul(width, height);
#endif
  mDrawable.mIsSurface = true;
    
  SetBitmap(&mDrawable, width, height, scale, drawScale);
}

SkiaBitmap::SkiaBitmap(const char* path, double sourceScale)
{
  auto data = SkData::MakeFromFileName(path);
  mDrawable.mImage = SkImage::MakeFromEncoded(data);

  mDrawable.mIsSurface = false;
  SetBitmap(&mDrawable, mDrawable.mImage->width(), mDrawable.mImage->height(), sourceScale, 1.f);
}

#pragma mark -

// Utility conversions
inline SkColor SkiaColor(const IColor& color, const IBlend* pBlend)
{
  if (pBlend)
    return SkColorSetARGB(Clip(static_cast<int>(pBlend->mWeight * color.A), 0, 255), color.R, color.G, color.B);
  else
    return SkColorSetARGB(color.A, color.R, color.G, color.B);
}

inline SkRect SkiaRect(const IRECT& r)
{
  return SkRect::MakeLTRB(r.L, r.T, r.R, r.B);
}

inline SkBlendMode SkiaBlendMode(const IBlend* pBlend)
{
  if (!pBlend)
    return SkBlendMode::kSrcOver;
    
  switch (pBlend->mMethod)
  {
    case EBlend::Default:         // fall through
    case EBlend::Clobber:         // fall through
    case EBlend::SourceOver:      return SkBlendMode::kSrcOver;
    case EBlend::SourceIn:        return SkBlendMode::kSrcIn;
    case EBlend::SourceOut:       return SkBlendMode::kSrcOut;
    case EBlend::SourceAtop:      return SkBlendMode::kSrcATop;
    case EBlend::DestOver:        return SkBlendMode::kDstOver;
    case EBlend::DestIn:          return SkBlendMode::kDstIn;
    case EBlend::DestOut:         return SkBlendMode::kDstOut;
    case EBlend::DestAtop:        return SkBlendMode::kDstATop;
    case EBlend::Add:             return SkBlendMode::kPlus;
    case EBlend::XOR:             return SkBlendMode::kXor;
  }
  
  return SkBlendMode::kClear;
}

inline SkTileMode SkiaTileMode(const IPattern& pattern)
{
  switch (pattern.mExtend)
  {
    case EPatternExtend::None:      return SkTileMode::kDecal;
    case EPatternExtend::Reflect:   return SkTileMode::kMirror;
    case EPatternExtend::Repeat:    return SkTileMode::kRepeat;
    case EPatternExtend::Pad:       return SkTileMode::kClamp;
  }

  return SkTileMode::kClamp;
}

SkPaint SkiaPaint(const IPattern& pattern, const IBlend* pBlend)
{
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setBlendMode(SkiaBlendMode(pBlend));
    
  if (pattern.mType == EPatternType::Solid || pattern.NStops() <  2)
  {
    paint.setColor(SkiaColor(pattern.GetStop(0).mColor, pBlend));
  }
  else
  {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 1.0;
      
    IMatrix m = pattern.mTransform;
    m.Invert();
    m.TransformPoint(x1, y1);
    m.TransformPoint(x2, y2);
      
    SkPoint points[2] =
    {
      SkPoint::Make(x1, y1),
      SkPoint::Make(x2, y2)
    };
    
    SkColor colors[8];
    SkScalar positions[8];
      
    assert(pattern.NStops() <= 8);
    
    for(int i = 0; i < pattern.NStops(); i++)
    {
      const IColorStop& stop = pattern.GetStop(i);
      colors[i] = SkiaColor(stop.mColor, pBlend);
      positions[i] = stop.mOffset;
    }
   
    if(pattern.mType == EPatternType::Linear)
      paint.setShader(SkGradientShader::MakeLinear(points, colors, positions, pattern.NStops(), SkiaTileMode(pattern), 0, nullptr));
    else
    {
      float xd = points[0].x() - points[1].x();
      float yd = points[0].y() - points[1].y();
      float radius = std::sqrt(xd * xd + yd * yd);
        
      paint.setShader(SkGradientShader::MakeRadial(points[0], radius, colors, positions, pattern.NStops(), SkiaTileMode(pattern), 0, nullptr));
    }
  }
    
  return paint;
}

#pragma mark -

IGraphicsSkia::IGraphicsSkia(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
: IGraphicsPathBase(dlg, w, h, fps, scale)
{
  DBGMSG("IGraphics Skia @ %i FPS\n", fps);
}

IGraphicsSkia::~IGraphicsSkia()
{
}

bool IGraphicsSkia::BitmapExtSupported(const char* ext)
{
  char extLower[32];
  ToLower(extLower, ext);
  return (strstr(extLower, "png") != nullptr) /*|| (strstr(extLower, "jpg") != nullptr) || (strstr(extLower, "jpeg") != nullptr)*/;
}

APIBitmap* IGraphicsSkia::LoadAPIBitmap(const char* fileNameOrResID, int scale, EResourceLocation location, const char* ext)
{
  return new SkiaBitmap(fileNameOrResID, scale);
}

void IGraphicsSkia::OnViewInitialized(void* pContext)
{
#if defined IGRAPHICS_GL
  int fbo = 0, samples = 0, stencilBits = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_SAMPLES, &samples);
  glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
  
  auto interface = GrGLMakeNativeInterface();
  mGrContext = GrContext::MakeGL(interface);
  
  GrGLFramebufferInfo fbinfo;
  fbinfo.fFBOID = fbo;
  fbinfo.fFormat = 0x8058;
  
  auto backendRenderTarget = GrBackendRenderTarget(WindowWidth(), WindowHeight(), samples, stencilBits, fbinfo);

  mSurface = SkSurface::MakeFromBackendRenderTarget(mGrContext.get(), backendRenderTarget, kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, nullptr, nullptr);
#else
  mSurface = SkSurface::MakeRasterN32Premul(WindowWidth() * GetScreenScale(), WindowHeight() * GetScreenScale());
#endif
  mCanvas = mSurface->getCanvas();
}

void IGraphicsSkia::OnViewDestroyed()
{
}

void IGraphicsSkia::DrawResize()
{
#if defined IGRAPHICS_CPU
   mSurface = SkSurface::MakeRasterN32Premul(WindowWidth() * GetScreenScale(), WindowHeight() * GetScreenScale());
   mCanvas = mSurface->getCanvas();
#endif
}

void IGraphicsSkia::BeginFrame()
{
  mCanvas->clear(SK_ColorWHITE);
}

void IGraphicsSkia::EndFrame()
{
#ifdef IGRAPHICS_CPU
  #ifdef OS_MAC
    SkPixmap pixmap;
    mSurface->peekPixels(&pixmap);
    SkBitmap bmp;
    bmp.installPixels(pixmap);
    CGContext* pCGContext = (CGContextRef) mPlatformContext;
    CGContextSaveGState(pCGContext);
    CGContextScaleCTM(pCGContext, 1.0 / GetScreenScale(), 1.0 / GetScreenScale());
    SkCGDrawBitmap(pCGContext, bmp, 0, 0);
    CGContextRestoreGState(pCGContext);
  #endif
#else
  mCanvas->flush();
#endif
}

void IGraphicsSkia::DrawBitmap(const IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend)
{
  SkPaint p;
  p.setFilterQuality(kHigh_SkFilterQuality);
  p.setBlendMode(SkiaBlendMode(pBlend));
  if (pBlend)
    p.setAlpha(Clip(static_cast<int>(pBlend->mWeight * 255), 0, 255));
    
  SkiaDrawable* image = bitmap.GetAPIBitmap()->GetBitmap();

  double scale = 1.0 / (bitmap.GetScale() * bitmap.GetDrawScale());
  
#ifdef IGRAPHICS_GL
  if (image->mIsSurface)
    scale = 1.0 / (bitmap.GetDrawScale());
#endif
  
  mCanvas->save();
  mCanvas->translate(dest.L, dest.T);
  mCanvas->scale(scale, scale);

  if (image->mIsSurface)
    image->mSurface->draw(mCanvas, 0.0, 0.0, &p);
  else
    mCanvas->drawImage(image->mImage, 0.0, 0.0, &p);
    
    mCanvas->restore();
}

IColor IGraphicsSkia::GetPoint(int x, int y)
{
  return COLOR_BLACK; //TODO:
}

bool IGraphicsSkia::LoadAPIFont(const char* fontID, const PlatformFontPtr& font)
{
    //SkFont font;
    return false;
}

void IGraphicsSkia::PrepareAndMeasureText(const IText& text, const char* str, IRECT& r, double& x, double & y, SkFont*& pFont) const
{
  SkFontMetrics metrics;
  SkPaint paint;
  SkRect bounds;
  
  //StaticStorage<CairoFont>::Accessor storage(sFontCache);
  static SkFont sFont;
  sFont.setSubpixel(true);
  sFont.setSize(text.mSize);
  
  pFont = &sFont;//storage.Find(text.mFont);
  
  assert(pFont && "No font found - did you forget to load it?");
  
  // Draw / measure

  pFont->measureText(str, strlen(str), SkTextEncoding::kUTF8, &bounds);
  pFont->getMetrics(&metrics);
  
  const double textWidth = bounds.width();// + textExtents.x_bearing;
  const double textHeight = text.mSize;
  const double ascender = metrics.fAscent;
  const double descender = metrics.fDescent;
  
  switch (text.mAlign)
  {
    case EAlign::Near:     x = r.L;                          break;
    case EAlign::Center:   x = r.MW() - (textWidth / 2.0);   break;
    case EAlign::Far:      x = r.R - textWidth;              break;
  }
  
  switch (text.mVAlign)
  {
    case EVAlign::Top:      y = r.T + textHeight;                            break;
    case EVAlign::Middle:   y = r.MH() - descender + (textHeight / 2.0);   break;
    case EVAlign::Bottom:   y = r.B - descender;                           break;
  }
  
  r = IRECT((float) x, (float) y - textHeight, (float) (x + textWidth), (float) (y));
}

void IGraphicsSkia::DoMeasureText(const IText& text, const char* str, IRECT& bounds) const
{
  SkFont* pFont;

  IRECT r = bounds;
  double x, y;
  PrepareAndMeasureText(text, str, bounds, x, y, pFont);
  DoMeasureTextRotation(text, r, bounds);
}

void IGraphicsSkia::DoDrawText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend)
{
  IRECT measured = bounds;
  
  SkFont* pFont;
  double x, y;
  
  PrepareAndMeasureText(text, str, measured, x, y, pFont);
  PathTransformSave();
  DoTextRotation(text, bounds, measured);
  SkPaint paint;
  paint.setColor(SkiaColor(text.mFGColor, pBlend));
  mCanvas->drawSimpleText(str, strlen(str), SkTextEncoding::kUTF8, x, y, *pFont, paint);
  PathTransformRestore();
}

void IGraphicsSkia::PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend)
{
  SkPaint paint = SkiaPaint(pattern, pBlend);
  paint.setStyle(SkPaint::kStroke_Style);

  switch (options.mCapOption)
  {
    case ELineCap::Butt:   paint.setStrokeCap(SkPaint::kButt_Cap);     break;
    case ELineCap::Round:  paint.setStrokeCap(SkPaint::kRound_Cap);    break;
    case ELineCap::Square: paint.setStrokeCap(SkPaint::kSquare_Cap);   break;
  }

  switch (options.mJoinOption)
  {
    case ELineJoin::Miter: paint.setStrokeJoin(SkPaint::kMiter_Join);   break;
    case ELineJoin::Round: paint.setStrokeJoin(SkPaint::kRound_Join);   break;
    case ELineJoin::Bevel: paint.setStrokeJoin(SkPaint::kBevel_Join);   break;
  }
  
  if (options.mDash.GetCount())
  {
    // N.B. support odd counts by reading the array twice
      
    int dashCount = options.mDash.GetCount();
    int dashMax = dashCount & 1 ? dashCount * 2 : dashCount;
    float dashArray[16];
      
    for (int i = 0; i < dashMax; i += 2)
    {
        dashArray[i + 0] = options.mDash.GetArray()[i % dashCount];
        dashArray[i + 1] = options.mDash.GetArray()[(i + 1) % dashCount];
    }
    
    paint.setPathEffect(SkDashPathEffect::Make(dashArray, dashMax, 0));
  }
  
  paint.setStrokeWidth(thickness);
  paint.setStrokeMiter(options.mMiterLimit);
  
  mCanvas->drawPath(mMainPath, paint);
  
  if (!options.mPreserve)
    mMainPath.reset();
}

void IGraphicsSkia::PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend)
{
  SkPaint paint = SkiaPaint(pattern, pBlend);
  paint.setStyle(SkPaint::kFill_Style);
  
  if (options.mFillRule == EFillRule::Winding)
    mMainPath.setFillType(SkPath::kWinding_FillType);
  else
    mMainPath.setFillType(SkPath::kEvenOdd_FillType);
  
  mCanvas->drawPath(mMainPath, paint);
  
  if (!options.mPreserve)
    mMainPath.reset();
}

void IGraphicsSkia::PathTransformSetMatrix(const IMatrix& m)
{
  double scale = 1.0;
  double xTranslate = 0.0;
  double yTranslate = 0.0;
    
  if (!mCanvas)
    return;
    
  if (!mLayers.empty())
  {
    IRECT bounds = mLayers.top()->Bounds();
    
    xTranslate = -bounds.L;
    yTranslate = -bounds.T;
  }
    
#if defined IGRAPHICS_CPU
  scale = GetScreenScale() * GetDrawScale();
#endif
    
  SkMatrix globalMatrix = SkMatrix::MakeScale(scale);
  SkMatrix skMatrix = SkMatrix::MakeAll(m.mXX, m.mXY, m.mTX, m.mYX, m.mYY, m.mTY, 0, 0, 1);
  globalMatrix.preTranslate(xTranslate, yTranslate);
  skMatrix.postConcat(globalMatrix);
  mCanvas->setMatrix(skMatrix);
}

void IGraphicsSkia::SetClipRegion(const IRECT& r)
{
  SkRect skrect;
  skrect.set(r.L, r.T, r.R, r.B);
  mCanvas->restore();
  mCanvas->save();
  mCanvas->clipRect(skrect);
}

APIBitmap* IGraphicsSkia::CreateAPIBitmap(int width, int height, int scale, double drawScale)
{
  return new SkiaBitmap(mGrContext.get(), width, height, scale, drawScale);
}

void IGraphicsSkia::UpdateLayer()
{
    mCanvas = mLayers.empty() ? mSurface->getCanvas() : mLayers.top()->GetAPIBitmap()->GetBitmap()->mSurface->getCanvas();
}

size_t CalcRowBytes(int width)
{
    width = ((width + 7) & (-8));
    return width * sizeof(uint32_t);
}

void IGraphicsSkia::GetLayerBitmapData(const ILayerPtr& layer, RawBitmapData& data)
{
  SkiaDrawable* pDrawable = layer->GetAPIBitmap()->GetBitmap();
  size_t rowBytes = CalcRowBytes(pDrawable->mSurface->width());
  int size = pDrawable->mSurface->height() * static_cast<int>(rowBytes);
    
  data.Resize(size);
   
  if (data.GetSize() >= size)
  {
      SkImageInfo info = SkImageInfo::MakeN32Premul(pDrawable->mSurface->width(), pDrawable->mSurface->height());
      pDrawable->mSurface->readPixels(info, data.Get(), rowBytes, 0, 0);
  }
}

void IGraphicsSkia::ApplyShadowMask(ILayerPtr& layer, RawBitmapData& mask, const IShadow& shadow)
{
  SkiaDrawable* pDrawable = layer->GetAPIBitmap()->GetBitmap();
  int width = pDrawable->mSurface->width();
  int height = pDrawable->mSurface->height();
  size_t rowBytes = CalcRowBytes(width);
  double scale = layer->GetAPIBitmap()->GetDrawScale() * layer->GetAPIBitmap()->GetScale();
  
  SkCanvas* pCanvas = pDrawable->mSurface->getCanvas();
    
  SkMatrix m;
  m.reset();
    
  SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
  SkPixmap pixMap(info, mask.Get(), rowBytes);
  sk_sp<SkImage> image = SkImage::MakeFromRaster(pixMap, nullptr, nullptr);
  sk_sp<SkImage> foreground;
    
  // Copy the foreground if needed
    
  if (shadow.mDrawForeground)
    foreground = pDrawable->mSurface->makeImageSnapshot();
 
  pCanvas->clear(SK_ColorTRANSPARENT);
 
  IBlend blend(EBlend::Default, shadow.mOpacity);
  pCanvas->setMatrix(m);
  pCanvas->drawImage(image.get(), shadow.mXOffset * scale, shadow.mYOffset * scale);
  m = SkMatrix::MakeScale(scale);
  pCanvas->setMatrix(m);
  pCanvas->translate(-layer->Bounds().L, -layer->Bounds().T);
  SkPaint p = SkiaPaint(shadow.mPattern, &blend);
  p.setBlendMode(SkBlendMode::kSrcIn);
  pCanvas->drawPaint(p);

  if (shadow.mDrawForeground)
  {
    m.reset();
    pCanvas->setMatrix(m);
    pCanvas->drawImage(foreground.get(), 0.0, 0.0);
  }
}
