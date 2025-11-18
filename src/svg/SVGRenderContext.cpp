/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "SVGRenderContext.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include "core/utils/AddressOf.h"
#include "../../include/tgfx/core/Log.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/node/SVGClipPath.h"
#include "tgfx/svg/node/SVGFilter.h"
#include "tgfx/svg/node/SVGMask.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {
namespace {

LineCap toCap(const SVGLineCap& cap) {
  switch (cap) {
    case SVGLineCap::Butt:
      return LineCap::Butt;
    case SVGLineCap::Round:
      return LineCap::Round;
    case SVGLineCap::Square:
      return LineCap::Square;
  }
}

LineJoin toJoin(const SVGLineJoin& join) {
  switch (join.type()) {
    case SVGLineJoin::Type::Miter:
      return LineJoin::Miter;
    case SVGLineJoin::Type::Round:
      return LineJoin::Round;
    case SVGLineJoin::Type::Bevel:
      return LineJoin::Bevel;
    default:
      ASSERT(false);
      return LineJoin::Miter;
  }
}

std::shared_ptr<PathEffect> dash_effect(const SVGPresentationAttributes& props,
                                        const SVGLengthContext& lengthContext) {
  if (props.StrokeDashArray->type() != SVGDashArray::Type::DashArray) {
    return nullptr;
  }

  const auto& da = *props.StrokeDashArray;
  const auto count = da.dashArray().size();
  std::vector<float> intervals;
  intervals.reserve(count);
  for (const auto& dash : da.dashArray()) {
    intervals.push_back(lengthContext.resolve(dash, SVGLengthContext::LengthType::Other));
  }

  if (count & 1) {
    // If an odd number of values is provided, then the list of values
    // is repeated to yield an even number of values.
    intervals.resize(count * 2);
    memcpy(intervals.data() + count, intervals.data(), count * sizeof(float));
  }

  ASSERT((intervals.size() & 1) == 0);

  const auto phase =
      lengthContext.resolve(*props.StrokeDashOffset, SVGLengthContext::LengthType::Other);

  return PathEffect::MakeDash(intervals.data(), static_cast<int>(intervals.size()), phase);
}

}  // namespace

SVGPresentationContext::SVGPresentationContext()
    : _inherited(SVGPresentationAttributes::MakeInitial()) {
}

SVGRenderContext::SVGRenderContext(Canvas* canvas, const std::shared_ptr<TextShaper>& textShaper,
                                   const SVGIDMapper& mapper, const SVGLengthContext& lengthContext,
                                   const SVGPresentationContext& presentContext,
                                   const OBBScope& scope, const Matrix& matrix)
    : _textShaper(textShaper), nodeIDMapper(mapper), _lengthContext(lengthContext),
      _presentationContext(presentContext), _canvas(canvas),
      canvasSaveCount(canvas->getSaveCount()), scope(scope), _matrix(matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other)
    : SVGRenderContext(other._canvas, other._textShaper, other.nodeIDMapper, *other._lengthContext,
                       *other._presentationContext, other.scope, other._matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas)
    : SVGRenderContext(canvas, other._textShaper, other.nodeIDMapper, *other._lengthContext,
                       *other._presentationContext, other.scope, other._matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other,
                                   const SVGLengthContext& lengthContext)
    : SVGRenderContext(other._canvas, other._textShaper, other.nodeIDMapper, lengthContext,
                       *other._presentationContext, other.scope, other._matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas,
                                   const SVGLengthContext& lengthContext)
    : SVGRenderContext(canvas, other._textShaper, other.nodeIDMapper, lengthContext,
                       *other._presentationContext, other.scope, other._matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, const SVGNode* node)
    : SVGRenderContext(other._canvas, other._textShaper, other.nodeIDMapper, *other._lengthContext,
                       *other._presentationContext, OBBScope{node, this}, other._matrix) {
}

SVGRenderContext::~SVGRenderContext() {
  _canvas->restoreToCount(canvasSaveCount);
}

SVGRenderContext SVGRenderContext::CopyForPaint(const SVGRenderContext& context, Canvas* canvas,
                                                const SVGLengthContext& lengthContext) {
  SVGRenderContext copyContext(context, canvas, lengthContext);
  copyContext.deferredPaintOpacity = context.deferredPaintOpacity;
  return copyContext;
}

std::shared_ptr<SVGNode> SVGRenderContext::findNodeById(const SVGIRI& iri) const {
  if (iri.type() != SVGIRI::Type::Local) {
    return nullptr;
  }
  auto iter = nodeIDMapper.find(iri.iri());
  if (iter != nodeIDMapper.end()) {
    return iter->second;
  }
  return nullptr;
}

void SVGRenderContext::applyPresentationAttributes(const SVGPresentationAttributes& attrs,
                                                   uint32_t flags) {
#define ApplyLazyInheritedAttribute(ATTR)                                    \
  do {                                                                       \
    /* All attributes should be defined on the inherited context. */         \
    ASSERT(_presentationContext->_inherited.ATTR.isValue());                 \
    const auto& attr = attrs.ATTR;                                           \
    if (attr.isValue() && *attr != *_presentationContext->_inherited.ATTR) { \
      /* Update the local attribute value */                                 \
      _presentationContext.writable()->_inherited.ATTR.set(*attr);           \
    }                                                                        \
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

  if (attrs.ClipPath.isValue()) {
    saveOnce();
    _canvas->clipPath(applyClip(*attrs.ClipPath));
  }

  const bool hasFilter = attrs.Filter.isValue();
  if (attrs.Opacity.isValue()) {
    _canvas->saveLayerAlpha(this->applyOpacity(*attrs.Opacity, flags, hasFilter));
  }

  if (attrs.Mask.isValue()) {
    if (auto maskFilter = this->applyMask(*attrs.Mask)) {
      Paint maskPaint;
      maskPaint.setMaskFilter(maskFilter);
      _canvas->saveLayer(&maskPaint);
    }
  }

  if (hasFilter) {
    if (auto imageFilter = this->applyFilter(*attrs.Filter)) {
      Paint filterPaint;
      filterPaint.setImageFilter(imageFilter);
      _canvas->saveLayer(&filterPaint);
    }
  }
}

float SVGRenderContext::applyOpacity(float opacity, uint32_t flags, bool hasFilter) {
  opacity = std::clamp(opacity, 0.0f, 1.0f);
  const auto& props = _presentationContext->_inherited;
  const bool hasFill = props.Fill->type() != SVGPaint::Type::None;
  const bool hasStroke = props.Stroke->type() != SVGPaint::Type::None;

  // We can apply the opacity as paint alpha if it only affects one atomic draw.
  // For now, this means all of the following must be true:
  //   - the target node doesn't have any descendants;
  //   - it only has a stroke or a fill (but not both);
  //   - it does not have a filter.
  // Going forward, we may needto refine this heuristic (e.g. to accommodate markers).
  if ((flags & static_cast<uint32_t>(ApplyFlags::Leaf)) && (hasFill ^ hasStroke) && !hasFilter) {
    deferredPaintOpacity *= opacity;
    return 1.0f;
  }
  return opacity;
}

std::shared_ptr<ImageFilter> SVGRenderContext::applyFilter(const SVGFuncIRI& filter) const {
  if (filter.type() != SVGFuncIRI::Type::IRI) {
    return nullptr;
  }

  const auto node = this->findNodeById(filter.iri());
  if (!node || node->tag() != SVGTag::Filter) {
    return nullptr;
  }

  auto filterNode = reinterpret_cast<const SVGFilter*>(node.get());
  return filterNode->buildFilterDAG(*this);
}

void SVGRenderContext::saveOnce() {
  if (_canvas->getSaveCount() == canvasSaveCount) {
    _canvas->save();
  }
}

Path SVGRenderContext::applyClip(const SVGFuncIRI& clip) const {
  if (clip.type() != SVGFuncIRI::Type::IRI) {
    return Path();
  }

  const auto clipNode = this->findNodeById(clip.iri());
  if (!clipNode || clipNode->tag() != SVGTag::ClipPath) {
    return Path();
  }

  return static_cast<const SVGClipPath*>(clipNode.get())->resolveClip(*this);
}

std::shared_ptr<MaskFilter> SVGRenderContext::applyMask(const SVGFuncIRI& mask) {
  if (mask.type() != SVGFuncIRI::Type::IRI) {
    return nullptr;
  }

  const auto node = this->findNodeById(mask.iri());
  if (!node || node->tag() != SVGTag::Mask) {
    return nullptr;
  }

  auto maskNode = std::static_pointer_cast<SVGMask>(node);
  PictureRecorder maskRecorder;
  auto maskCanvas = maskRecorder.beginRecording();
  {
    SVGRenderContext maskContext(*this, maskCanvas);
    maskNode->renderMask(maskContext);
  }
  auto picture = maskRecorder.finishRecordingAsPicture();
  if (!picture) {
    return nullptr;
  }

  auto bounds = picture->getBounds();
  bounds.roundOut();

  auto transMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  auto shaderImage = Image::MakeFrom(picture, static_cast<int>(bounds.width()),
                                     static_cast<int>(bounds.height()), &transMatrix);

  auto shader = Shader::MakeImageShader(shaderImage, TileMode::Decal, TileMode::Decal);
  if (shader) {
    Matrix shaderMatrix = _matrix;
    shaderMatrix.preTranslate(bounds.left, bounds.top);
    shader = shader->makeWithMatrix(shaderMatrix);
  }
  return MaskFilter::MakeShader(shader);
}

std::optional<Paint> SVGRenderContext::commonPaint(const SVGPaint& svgPaint, float opacity) const {
  if (svgPaint.type() == SVGPaint::Type::None) {
    return std::optional<Paint>();
  }

  std::optional<Paint> paint = Paint();
  switch (svgPaint.type()) {
    case SVGPaint::Type::Color:
      paint->setColor(this->resolveSVGColor(svgPaint.color()));
      break;
    case SVGPaint::Type::IRI: {
      // Our property inheritance is borked as it follows the render path and not the tree
      // hierarchy.  To avoid gross transgressions like leaf node presentation attributes
      // leaking into the paint server context, use a pristine presentation context when
      // following hrefs.
      //
      // Preserve the OBB scope because some paints use object bounding box coords
      // (e.g. gradient control points), which requires access to the render context
      // and node being rendered.
      SVGPresentationContext presentContext;
      presentContext._namedColors = _presentationContext->_namedColors;
      SVGRenderContext localContext(_canvas, _textShaper, nodeIDMapper, *_lengthContext,
                                    presentContext, scope, {});

      const auto node = this->findNodeById(svgPaint.iri());
      if (!node || !node->asPaint(localContext, AddressOf(paint))) {
        // Use the fallback color.
        paint->setColor(this->resolveSVGColor(svgPaint.color()));
      }
    } break;
    default:
      break;
  }
  paint->setAntiAlias(true);

  // We observe 3 opacity components:
  //   - initial paint server opacity (e.g. color stop opacity)
  //   - paint-specific opacity (e.g. 'fill-opacity', 'stroke-opacity')
  //   - deferred opacity override (optimization for leaf nodes 'opacity')
  paint->setAlpha(std::clamp(paint->getAlpha() * opacity * deferredPaintOpacity, 0.0f, 1.0f));
  return paint;
}

std::optional<Paint> SVGRenderContext::fillPaint() const {
  const auto& props = _presentationContext->_inherited;
  auto paint = this->commonPaint(*props.Fill, *props.FillOpacity);

  if (paint.has_value()) {
    paint->setStyle(PaintStyle::Fill);
  }

  return paint;
}

std::optional<Paint> SVGRenderContext::strokePaint() const {
  const auto& props = _presentationContext->_inherited;
  auto paint = this->commonPaint(*props.Stroke, *props.StrokeOpacity);

  if (paint.has_value()) {
    paint->setStyle(PaintStyle::Stroke);
    Stroke stroke;
    stroke.width = _lengthContext->resolve(*props.StrokeWidth, SVGLengthContext::LengthType::Other);
    stroke.cap = toCap(*props.StrokeLineCap);
    stroke.join = toJoin(*props.StrokeLineJoin);
    stroke.miterLimit = *props.StrokeMiterLimit;
    paint->setStroke(stroke);
  }
  return paint;
}

std::shared_ptr<PathEffect> SVGRenderContext::strokePathEffect() const {
  const auto& props = _presentationContext->_inherited;
  return dash_effect(props, *_lengthContext);
}

SVGColorType SVGRenderContext::resolveSVGColor(const SVGColor& color) const {
  if (_presentationContext->_namedColors) {
    for (auto&& ident : *color.vars()) {
      auto iter = _presentationContext->_namedColors->find(ident);
      if (iter != _presentationContext->_namedColors->end()) {
        return iter->second;
      }
    }
  }
  switch (color.type()) {
    case SVGColor::Type::Color:
      return color.color();
    case SVGColor::Type::CurrentColor:
      return *_presentationContext->_inherited.Color;
    case SVGColor::Type::ICCColor:
      return Color::Black();
  }
}

float SVGRenderContext::resolveFontSize() const {
  return lengthContext().resolve(presentationContext()._inherited.FontSize->size(),
                                 SVGLengthContext::LengthType::Vertical);
}

std::shared_ptr<Typeface> SVGRenderContext::resolveTypeface() const {

  auto weight = [](const SVGFontWeight& w) {
    switch (w.type()) {
      case SVGFontWeight::Type::W100:
        return FontWeight::Thin;
      case SVGFontWeight::Type::W200:
        return FontWeight::ExtraLight;
      case SVGFontWeight::Type::W300:
        return FontWeight::Light;
      case SVGFontWeight::Type::W400:
        return FontWeight::Normal;
      case SVGFontWeight::Type::W500:
        return FontWeight::Medium;
      case SVGFontWeight::Type::W600:
        return FontWeight::SemiBold;
      case SVGFontWeight::Type::W700:
        return FontWeight::Bold;
      case SVGFontWeight::Type::W800:
        return FontWeight::ExtraBold;
      case SVGFontWeight::Type::W900:
        return FontWeight::Black;
      case SVGFontWeight::Type::Normal:
        return FontWeight::Normal;
      case SVGFontWeight::Type::Bold:
        return FontWeight::Bold;
      case SVGFontWeight::Type::Bolder:
        return FontWeight::ExtraBold;
      case SVGFontWeight::Type::Lighter:
        return FontWeight::Light;
      case SVGFontWeight::Type::Inherit: {
        return FontWeight::Normal;
      }
    }
  };

  auto slant = [](const SVGFontStyle& s) {
    switch (s.type()) {
      case SVGFontStyle::Type::Normal:
        return FontSlant::Upright;
      case SVGFontStyle::Type::Italic:
        return FontSlant::Italic;
      case SVGFontStyle::Type::Oblique:
        return FontSlant::Oblique;
      case SVGFontStyle::Type::Inherit: {
        return FontSlant::Upright;
      }
    }
  };

  const auto& family = presentationContext()._inherited.FontFamily->family();
  const FontStyle style(weight(*presentationContext()._inherited.FontWeight), FontWidth::Normal,
                        slant(*presentationContext()._inherited.FontStyle));
  return Typeface::MakeFromName(family, style);
}

SVGRenderContext::OBBTransform SVGRenderContext::transformForCurrentBoundBox(
    SVGObjectBoundingBoxUnits unit) const {
  if (!scope.node || unit.type() == SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse) {
    return {{0, 0}, {1, 1}};
  }
  ASSERT(scope.context);

  const auto obb = scope.node->objectBoundingBox(*scope.context);
  return {{obb.x(), obb.y()}, {obb.width(), obb.height()}};
}

Rect SVGRenderContext::resolveOBBRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                                      const SVGLength& h, SVGObjectBoundingBoxUnits unit) const {
  CopyOnWrite<SVGLengthContext> lengthContext(_lengthContext);

  if (unit.type() == SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox) {
    *lengthContext.writable() = SVGLengthContext({1, 1});
  }

  auto r = lengthContext->resolveRect(x, y, w, h);
  const auto transform = this->transformForCurrentBoundBox(unit);

  return Rect::MakeXYWH(transform.scale.x * r.x() + transform.offset.x,
                        transform.scale.y * r.y() + transform.offset.y,
                        transform.scale.x * r.width(), transform.scale.y * r.height());
}

}  // namespace tgfx