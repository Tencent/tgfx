/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/svg/SVGRenderContext.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <optional>
#include "core/utils/Log.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/svg/node/SVGClipPath.h"
#include "tgfx/svg/node/SVGFilter.h"
#include "tgfx/svg/node/SVGMask.h"
#include "tgfx/svg/node/SVGNode.h"

#ifndef RENDER_SVG

namespace tgfx {

namespace {

float length_size_for_type(const Size& viewport, SVGLengthContext::LengthType t) {
  switch (t) {
    case SVGLengthContext::LengthType::Horizontal:
      return viewport.width;
    case SVGLengthContext::LengthType::Vertical:
      return viewport.height;
    case SVGLengthContext::LengthType::Other: {
      // https://www.w3.org/TR/SVG11/coords.html#Units_viewport_percentage
      const float rsqrt2 = 1.0f / std::sqrt(2.0f);
      const float w = viewport.width;
      const float h = viewport.height;
      return rsqrt2 * std::sqrt(w * w + h * h);
    }
  }
  ASSERT(false);  // Not reached.
  return 0;
}

// Multipliers for DPI-relative units.
constexpr float kINMultiplier = 1.00f;
constexpr float kPTMultiplier = kINMultiplier / 72.272f;
constexpr float kPCMultiplier = kPTMultiplier * 12;
constexpr float kMMMultiplier = kINMultiplier / 25.4f;
constexpr float kCMMultiplier = kMMMultiplier * 10;

}  // namespace

float SVGLengthContext::resolve(const SVGLength& l, LengthType t) const {
  switch (l.unit()) {
    case SVGLength::Unit::kNumber: {
      if (_patternUnit.has_value()) {
        if (_patternUnit.value().type() == SVGObjectBoundingBoxUnits::Type::kObjectBoundingBox) {
          return l.value() * length_size_for_type(_viewport, t);
        } else {
          return l.value();
        }
      } else {
        return l.value();
      }
    }
    case SVGLength::Unit::kPX:
      return l.value();
    case SVGLength::Unit::kPercentage:
      return l.value() * length_size_for_type(_viewport, t) / 100;
    case SVGLength::Unit::kCM:
      return l.value() * _dpi * kCMMultiplier;
    case SVGLength::Unit::kMM:
      return l.value() * _dpi * kMMMultiplier;
    case SVGLength::Unit::kIN:
      return l.value() * _dpi * kINMultiplier;
    case SVGLength::Unit::kPT:
      return l.value() * _dpi * kPTMultiplier;
    case SVGLength::Unit::kPC:
      return l.value() * _dpi * kPCMultiplier;
    default:
      //unsupported unit type
      ASSERT(false);
      return 0;
  }
}

Rect SVGLengthContext::resolveRect(const SVGLength& x, const SVGLength& y, const SVGLength& width,
                                   const SVGLength& height) const {
  return Rect::MakeXYWH(this->resolve(x, SVGLengthContext::LengthType::Horizontal),
                        this->resolve(y, SVGLengthContext::LengthType::Vertical),
                        this->resolve(width, SVGLengthContext::LengthType::Horizontal),
                        this->resolve(height, SVGLengthContext::LengthType::Vertical));
}

namespace {

LineCap toSkCap(const SVGLineCap& cap) {
  switch (cap) {
    case SVGLineCap::kButt:
      return LineCap::Butt;
    case SVGLineCap::kRound:
      return LineCap::Round;
    case SVGLineCap::kSquare:
      return LineCap::Square;
  }
}

LineJoin toSkJoin(const SVGLineJoin& join) {
  switch (join.type()) {
    case SVGLineJoin::Type::kMiter:
      return LineJoin::Miter;
    case SVGLineJoin::Type::kRound:
      return LineJoin::Round;
    case SVGLineJoin::Type::kBevel:
      return LineJoin::Bevel;
    default:
      ASSERT(false);
      return LineJoin::Miter;
  }
}

std::unique_ptr<PathEffect> dash_effect(const SVGPresentationAttributes& props,
                                        const SVGLengthContext& lctx) {
  if (props.fStrokeDashArray->type() != SVGDashArray::Type::kDashArray) {
    return nullptr;
  }

  const auto& da = *props.fStrokeDashArray;
  const auto count = da.dashArray().size();
  std::vector<float> intervals;
  intervals.reserve(count);
  for (const auto& dash : da.dashArray()) {
    intervals.push_back(lctx.resolve(dash, SVGLengthContext::LengthType::Other));
  }

  if (count & 1) {
    // If an odd number of values is provided, then the list of values
    // is repeated to yield an even number of values.
    intervals.resize(count * 2);
    memcpy(intervals.data() + count, intervals.data(), count * sizeof(float));
  }

  ASSERT((intervals.size() & 1) == 0);

  const auto phase = lctx.resolve(*props.fStrokeDashOffset, SVGLengthContext::LengthType::Other);

  return PathEffect::MakeDash(intervals.data(), static_cast<int>(intervals.size()), phase);
}

}  // namespace

SkSVGPresentationContext::SkSVGPresentationContext()
    : _inherited(SVGPresentationAttributes::MakeInitial()) {
}

SVGRenderContext::SVGRenderContext(Context* device, Canvas* canvas,
                                   const std::shared_ptr<SVGFontManager>& fontManager,
                                   const SVGIDMapper& mapper, const SVGLengthContext& lctx,
                                   const SkSVGPresentationContext& pctx, const OBBScope& obbs)
    : _fontMgr(fontManager), _nodeIDMapper(mapper), _lengthContext(lctx),
      _presentationContext(pctx), _renderCanvas(canvas), _recorder(),
      _canvas(_recorder.beginRecording()), _scope(obbs), _deviceContext(device) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other)
    : SVGRenderContext(other._deviceContext, other._canvas, other._fontMgr, other._nodeIDMapper,
                       *other._lengthContext, *other._presentationContext, other._scope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas)
    : SVGRenderContext(other._deviceContext, canvas, other._fontMgr, other._nodeIDMapper,
                       *other._lengthContext, *other._presentationContext, other._scope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, const SVGLengthContext& lengthCtx)
    : SVGRenderContext(other._deviceContext, other._canvas, other._fontMgr, other._nodeIDMapper,
                       lengthCtx, *other._presentationContext, other._scope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas,
                                   const SVGLengthContext& lengthCtx)
    : SVGRenderContext(other._deviceContext, canvas, other._fontMgr, other._nodeIDMapper, lengthCtx,
                       *other._presentationContext, other._scope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, const SVGNode* node)
    : SVGRenderContext(other._deviceContext, other._canvas, other._fontMgr, other._nodeIDMapper,
                       *other._lengthContext, *other._presentationContext, OBBScope{node, this}) {
}

SVGRenderContext::~SVGRenderContext() {
  _canvas->restoreToCount(0);
  auto picture = _recorder.finishRecordingAsPicture();
  if (!picture) {
    return;
  }

  _renderCanvas->save();
  if (_clipPath.has_value()) {
    _renderCanvas->clipPath(_clipPath.value());
  }
  auto matrix = Matrix::I();
  _renderCanvas->drawPicture(picture, &matrix, &_picturePaint);
  _renderCanvas->restore();
}

SVGRenderContext SVGRenderContext::CopyForPaint(const SVGRenderContext& context, Canvas* canvas,
                                                const SVGLengthContext& lengthCtx) {
  SVGRenderContext copyContext(context, canvas, lengthCtx);
  copyContext._deferredPaintOpacity = context._deferredPaintOpacity;
  return copyContext;
}

std::shared_ptr<SVGNode> SVGRenderContext::findNodeById(const SVGIRI& iri) const {
  if (iri.type() != SVGIRI::Type::kLocal) {
    return nullptr;
  }
  auto p = _nodeIDMapper.find(iri.iri())->second;
  return p;
}

void SVGRenderContext::applyPresentationAttributes(const SVGPresentationAttributes& attrs,
                                                   uint32_t flags) {
#define ApplyLazyInheritedAttribute(ATTR)                                       \
  do {                                                                          \
    /* All attributes should be defined on the inherited context. */            \
    ASSERT(_presentationContext->_inherited.f##ATTR.isValue());                 \
    const auto& attr = attrs.f##ATTR;                                           \
    if (attr.isValue() && *attr != *_presentationContext->_inherited.f##ATTR) { \
      /* Update the local attribute value */                                    \
      _presentationContext.writable()->_inherited.f##ATTR.set(*attr);           \
    }                                                                           \
  } while (false)

  ApplyLazyInheritedAttribute(Fill);
  ApplyLazyInheritedAttribute(FillOpacity);
  ApplyLazyInheritedAttribute(FillRule);
  ApplyLazyInheritedAttribute(FontFamily);
  ApplyLazyInheritedAttribute(FontSize);
  ApplyLazyInheritedAttribute(FontStyle);
  ApplyLazyInheritedAttribute(FontWeight);
  ApplyLazyInheritedAttribute(ClipRule);
  ApplyLazyInheritedAttribute(Stroke);
  ApplyLazyInheritedAttribute(StrokeDashOffset);
  ApplyLazyInheritedAttribute(StrokeDashArray);
  ApplyLazyInheritedAttribute(StrokeLineCap);
  ApplyLazyInheritedAttribute(StrokeLineJoin);
  ApplyLazyInheritedAttribute(StrokeMiterLimit);
  ApplyLazyInheritedAttribute(StrokeOpacity);
  ApplyLazyInheritedAttribute(StrokeWidth);
  ApplyLazyInheritedAttribute(TextAnchor);
  ApplyLazyInheritedAttribute(Visibility);
  ApplyLazyInheritedAttribute(Color);
  ApplyLazyInheritedAttribute(ColorInterpolation);
  ApplyLazyInheritedAttribute(ColorInterpolationFilters);

#undef ApplyLazyInheritedAttribute

  if (attrs.fClipPath.isValue()) {
    _clipPath = this->applyClip(*attrs.fClipPath);
  }

  const bool hasFilter = attrs.fFilter.isValue();
  if (attrs.fOpacity.isValue()) {
    _picturePaint.setAlpha(this->applyOpacity(*attrs.fOpacity, flags, hasFilter));
  }

  if (attrs.fMask.isValue()) {
    if (auto maskFilter = this->applyMask(*attrs.fMask)) {
      _picturePaint.setMaskFilter(maskFilter);
    }
  }

  if (hasFilter) {
    if (auto imageFilter = this->applyFilter(*attrs.fFilter)) {
      _picturePaint.setImageFilter(imageFilter);
    }
  }
}

float SVGRenderContext::applyOpacity(float opacity, uint32_t flags, bool hasFilter) {
  opacity = std::clamp(opacity, 0.0f, 1.0f);
  const auto& props = _presentationContext->_inherited;
  const bool hasFill = props.fFill->type() != SVGPaint::Type::kNone;
  const bool hasStroke = props.fStroke->type() != SVGPaint::Type::kNone;

  // We can apply the opacity as paint alpha if it only affects one atomic draw.
  // For now, this means all of the following must be true:
  //   - the target node doesn't have any descendants;
  //   - it only has a stroke or a fill (but not both);
  //   - it does not have a filter.
  // Going forward, we may needto refine this heuristic (e.g. to accommodate markers).
  if ((flags & kLeaf) && (hasFill ^ hasStroke) && !hasFilter) {
    _deferredPaintOpacity *= opacity;
    return 1.0f;
  } else {
    return opacity;
  }
}

std::shared_ptr<ImageFilter> SVGRenderContext::applyFilter(const SVGFuncIRI& filter) {
  if (filter.type() != SVGFuncIRI::Type::kIRI) {
    return nullptr;
  }

  const auto node = this->findNodeById(filter.iri());
  if (!node || node->tag() != SVGTag::kFilter) {
    return nullptr;
  }

  const auto* filterNode = reinterpret_cast<const SkSVGFilter*>(node.get());
  return filterNode->buildFilterDAG(*this);
}

void SVGRenderContext::saveOnce() {
  _canvas->save();
}

Path SVGRenderContext::applyClip(const SVGFuncIRI& clip) {
  if (clip.type() != SVGFuncIRI::Type::kIRI) {
    return Path();
  }

  const auto clipNode = this->findNodeById(clip.iri());
  if (!clipNode || clipNode->tag() != SVGTag::kClipPath) {
    return Path();
  }

  return static_cast<const SVGClipPath*>(clipNode.get())->resolveClip(*this);
}

std::shared_ptr<MaskFilter> SVGRenderContext::applyMask(const SVGFuncIRI& mask) {
  if (mask.type() != SVGFuncIRI::Type::kIRI) {
    return nullptr;
  }

  const auto node = this->findNodeById(mask.iri());
  if (!node || node->tag() != SVGTag::kMask) {
    return nullptr;
  }

  auto maskNode = std::static_pointer_cast<SkSVGMask>(node);
  auto maskBound = maskNode->bounds(*this);

  Recorder maskRecorder;
  auto* maskCanvas = maskRecorder.beginRecording();
  {
    SVGRenderContext maskCtx(*this, maskCanvas);
    maskNode->renderMask(maskCtx);
  }
  auto picture = maskRecorder.finishRecordingAsPicture();
  if (!picture) {
    return nullptr;
  }

  auto bound = picture->getBounds();
  maskBound.join(bound);

  // if (!_deviceContext) {
  //   return nullptr;
  // }
  // auto tempSurface = Surface::Make(_deviceContext, static_cast<int>(bound.width()),
  //                                  static_cast<int>(bound.height()));
  // if (!tempSurface) {
  //   return nullptr;
  // }
  // auto* tempCanvas = tempSurface->getCanvas();
  // tempCanvas->translate(bound.x(), bound.y());
  // // Matrix matrix = Matrix::I();
  // // Paint paint;
  // // paint.setColor(Color::Green());
  // // tempCanvas->drawPicture(picture, &matrix, &paint);
  // tempCanvas->drawPicture(picture);
  // // Paint paint;
  // // paint.setColor(Color::Black());
  // // tempCanvas->drawCircle(50, 50, 50, paint);
  // _deviceContext->flushAndSubmit();
  // auto shaderImage = tempSurface->makeImageSnapshot();
  // if (!shaderImage) {
  //   return nullptr;
  // }
  auto matrix = _canvas->getMatrix() * Matrix::MakeTrans(-maskBound.left, -maskBound.top);
  auto shaderImage = Image::MakeFrom(picture, static_cast<int>(bound.width()),
                                     static_cast<int>(bound.height()), &matrix);
  // {
  //   auto tempSurface = Surface::Make(_deviceContext, static_cast<int>(bound.width()) * 2,
  //                                    static_cast<int>(bound.height()) * 2);
  //   tempSurface->getCanvas()->clear();
  //   tempSurface->getCanvas()->drawPicture(picture);
  //   tgfx::Bitmap bitmap = {};
  //   bitmap.allocPixels(tempSurface->width(), tempSurface->height());
  //   auto* pixels = bitmap.lockPixels();
  //   auto success = tempSurface->readPixels(bitmap.info(), pixels);
  //   bitmap.unlockPixels();
  //   if (!success) {
  //     return nullptr;
  //   }
  //   auto imageData = bitmap.encode();
  //   imageData->bytes();

  //   std::ofstream out("/Users/yg/Downloads/yg.png", std::ios::binary);
  //   if (!out) {
  //     return nullptr;
  //   }
  //   out.write(reinterpret_cast<const char*>(imageData->data()),
  //             static_cast<std::streamsize>(imageData->size()));
  // }
  auto shader = Shader::MakeImageShader(shaderImage, TileMode::Decal, TileMode::Decal);
  if (!shader) {
    return nullptr;
  }
  // return nullptr;
  return MaskFilter::MakeShader(shader);
}

std::optional<Paint> SVGRenderContext::commonPaint(const SVGPaint& paint_selector,
                                                   float paint_opacity) const {
  if (paint_selector.type() == SVGPaint::Type::kNone) {
    return std::optional<Paint>();
  }

  std::optional<Paint> p = Paint();
  switch (paint_selector.type()) {
    case SVGPaint::Type::kColor:
      p->setColor(this->resolveSvgColor(paint_selector.color()));
      break;
    case SVGPaint::Type::kIRI: {
      // Our property inheritance is borked as it follows the render path and not the tree
      // hierarchy.  To avoid gross transgressions like leaf node presentation attributes
      // leaking into the paint server context, use a pristine presentation context when
      // following hrefs.
      //
      // Preserve the OBB scope because some paints use object bounding box coords
      // (e.g. gradient control points), which requires access to the render context
      // and node being rendered.
      SkSVGPresentationContext pctx;
      pctx._namedColors = _presentationContext->_namedColors;
      SVGRenderContext local_ctx(_deviceContext, _canvas, _fontMgr, _nodeIDMapper, *_lengthContext,
                                 pctx, _scope);

      const auto node = this->findNodeById(paint_selector.iri());
      if (!node || !node->asPaint(local_ctx, &(p.value()))) {
        // Use the fallback color.
        p->setColor(this->resolveSvgColor(paint_selector.color()));
      }
    } break;
    default:
      break;
  }
  p->setAntiAlias(true);  // TODO: shape-rendering support

  // We observe 3 opacity components:
  //   - initial paint server opacity (e.g. color stop opacity)
  //   - paint-specific opacity (e.g. 'fill-opacity', 'stroke-opacity')
  //   - deferred opacity override (optimization for leaf nodes 'opacity')
  p->setAlpha(std::clamp(p->getAlpha() * paint_opacity * _deferredPaintOpacity, 0.0f, 1.0f));
  return p;
}

std::optional<Paint> SVGRenderContext::fillPaint() const {
  const auto& props = _presentationContext->_inherited;
  auto p = this->commonPaint(*props.fFill, *props.fFillOpacity);

  if (p.has_value()) {
    p->setStyle(PaintStyle::Fill);
  }

  return p;
}

std::optional<Paint> SVGRenderContext::strokePaint() const {
  const auto& props = _presentationContext->_inherited;
  auto p = this->commonPaint(*props.fStroke, *props.fStrokeOpacity);

  if (p.has_value()) {
    p->setStyle(PaintStyle::Stroke);
    p->setStrokeWidth(
        _lengthContext->resolve(*props.fStrokeWidth, SVGLengthContext::LengthType::Other));
    Stroke stroke;
    stroke.cap = toSkCap(*props.fStrokeLineCap);
    stroke.join = toSkJoin(*props.fStrokeLineJoin);
    stroke.miterLimit = *props.fStrokeMiterLimit;
    p->setStroke(stroke);

    //TODO (YG)
    // p->setPathEffect(dash_effect(props, *fLengthContext));
    dash_effect(props, *_lengthContext);
  }

  return p;
}

SVGColorType SVGRenderContext::resolveSvgColor(const SVGColor& color) const {
  if (_presentationContext->_namedColors) {
    for (auto&& ident : *color.vars()) {
      auto iter = _presentationContext->_namedColors->find(ident);
      if (iter != _presentationContext->_namedColors->end()) {
        return iter->second;
      }
    }
  }
  switch (color.type()) {
    case SVGColor::Type::kColor:
      return color.color();
    case SVGColor::Type::kCurrentColor:
      return *_presentationContext->_inherited.fColor;
    case SVGColor::Type::kICCColor:
      return Color::Black();
  }
}

SVGRenderContext::OBBTransform SVGRenderContext::transformForCurrentOBB(
    SVGObjectBoundingBoxUnits u) const {
  if (!_scope.node || u.type() == SVGObjectBoundingBoxUnits::Type::kUserSpaceOnUse) {
    return {{0, 0}, {1, 1}};
  }
  ASSERT(_scope.context);

  const auto obb = _scope.node->objectBoundingBox(*_scope.context);
  return {{obb.x(), obb.y()}, {obb.width(), obb.height()}};
}

Rect SVGRenderContext::resolveOBBRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                                      const SVGLength& h, SVGObjectBoundingBoxUnits obbu) const {
  CopyOnWrite<SVGLengthContext> lctx(_lengthContext);

  if (obbu.type() == SVGObjectBoundingBoxUnits::Type::kObjectBoundingBox) {
    *lctx.writable() = SVGLengthContext({1, 1});
  }

  auto r = lctx->resolveRect(x, y, w, h);
  const auto obbt = this->transformForCurrentOBB(obbu);

  return Rect::MakeXYWH(obbt.scale.x * r.x() + obbt.offset.x, obbt.scale.y * r.y() + obbt.offset.y,
                        obbt.scale.x * r.width(), obbt.scale.y * r.height());
}
}  // namespace tgfx

#endif  // RENDER_SVG