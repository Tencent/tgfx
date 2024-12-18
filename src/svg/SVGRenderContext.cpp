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

#include "SVGRenderContext.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include "core/utils/Log.h"
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

SVGRenderContext::SVGRenderContext(Canvas* canvas,
                                   const std::shared_ptr<SVGFontManager>& fontManager,
                                   const SVGIDMapper& mapper, const SVGLengthContext& lengthContext,
                                   const SVGPresentationContext& presentContext,
                                   const OBBScope& obbs, const Matrix& matrix)
    : fontManager(fontManager), nodeIDMapper(mapper), _lengthContext(lengthContext),
      _presentationContext(presentContext), renderCanvas(canvas), recorder(),
      _canvas(recorder.beginRecording()), scope(obbs), matrix(matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other)
    : SVGRenderContext(other._canvas, other.fontManager, other.nodeIDMapper, *other._lengthContext,
                       *other._presentationContext, other.scope, other.matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas)
    : SVGRenderContext(canvas, other.fontManager, other.nodeIDMapper, *other._lengthContext,
                       *other._presentationContext, other.scope, other.matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, const SVGLengthContext& lengthCtx)
    : SVGRenderContext(other._canvas, other.fontManager, other.nodeIDMapper, lengthCtx,
                       *other._presentationContext, other.scope, other.matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas,
                                   const SVGLengthContext& lengthCtx)
    : SVGRenderContext(canvas, other.fontManager, other.nodeIDMapper, lengthCtx,
                       *other._presentationContext, other.scope, other.matrix) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, const SVGNode* node)
    : SVGRenderContext(other._canvas, other.fontManager, other.nodeIDMapper, *other._lengthContext,
                       *other._presentationContext, OBBScope{node, this}, other.matrix) {
}

SVGRenderContext::~SVGRenderContext() {
  _canvas->restoreToCount(0);
  auto picture = recorder.finishRecordingAsPicture();
  if (!picture) {
    return;
  }

  renderCanvas->save();
  if (_clipPath.has_value()) {
    renderCanvas->clipPath(_clipPath.value());
  }
  auto matrix = Matrix::I();
  renderCanvas->drawPicture(picture, &matrix, &picturePaint);
  renderCanvas->restore();
}

SVGRenderContext SVGRenderContext::CopyForPaint(const SVGRenderContext& context, Canvas* canvas,
                                                const SVGLengthContext& lengthCtx) {
  SVGRenderContext copyContext(context, canvas, lengthCtx);
  copyContext.deferredPaintOpacity = context.deferredPaintOpacity;
  return copyContext;
}

std::shared_ptr<SVGNode> SVGRenderContext::findNodeById(const SVGIRI& iri) const {
  if (iri.type() != SVGIRI::Type::Local) {
    return nullptr;
  }
  auto p = nodeIDMapper.find(iri.iri())->second;
  return p;
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
    _clipPath = this->applyClip(*attrs.ClipPath);
  }

  const bool hasFilter = attrs.Filter.isValue();
  if (attrs.Opacity.isValue()) {
    picturePaint.setAlpha(this->applyOpacity(*attrs.Opacity, flags, hasFilter));
  }

  if (attrs.Mask.isValue()) {
    if (auto maskFilter = this->applyMask(*attrs.Mask)) {
      picturePaint.setMaskFilter(maskFilter);
    }
  }

  if (hasFilter) {
    if (auto imageFilter = this->applyFilter(*attrs.Filter)) {
      picturePaint.setImageFilter(imageFilter);
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
  if ((flags & kLeaf) && (hasFill ^ hasStroke) && !hasFilter) {
    deferredPaintOpacity *= opacity;
    return 1.0f;
  } else {
    return opacity;
  }
}

std::shared_ptr<ImageFilter> SVGRenderContext::applyFilter(const SVGFuncIRI& filter) const {
  if (filter.type() != SVGFuncIRI::Type::IRI) {
    return nullptr;
  }

  const auto node = this->findNodeById(filter.iri());
  if (!node || node->tag() != SVGTag::Filter) {
    return nullptr;
  }

  const auto* filterNode = reinterpret_cast<const SVGFilter*>(node.get());
  return filterNode->buildFilterDAG(*this);
}

void SVGRenderContext::saveOnce() {
  _canvas->save();
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

  auto transMatrix = matrix * Matrix::MakeTrans(-maskBound.left, -maskBound.top);
  auto shaderImage =
      Image::MakeFrom(picture, static_cast<int>(bound.width() * matrix.getScaleX()),
                      static_cast<int>(bound.height() * matrix.getScaleY()), &transMatrix);
  auto shader = Shader::MakeImageShader(shaderImage, TileMode::Decal, TileMode::Decal);
  if (!shader) {
    return nullptr;
  }
  // return nullptr;
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
      SVGRenderContext localContext(_canvas, fontManager, nodeIDMapper, *_lengthContext,
                                    presentContext, scope, Matrix::I());

      const auto node = this->findNodeById(svgPaint.iri());
      if (!node || !node->asPaint(localContext, &(paint.value()))) {
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
    paint->setStrokeWidth(
        _lengthContext->resolve(*props.StrokeWidth, SVGLengthContext::LengthType::Other));
    Stroke stroke;
    stroke.cap = toCap(*props.StrokeLineCap);
    stroke.join = toJoin(*props.StrokeLineJoin);
    stroke.miterLimit = *props.StrokeMiterLimit;
    paint->setStroke(stroke);

    //TODO (YGAurora): Implement stroke dash array use PathEffect
    dash_effect(props, *_lengthContext);
  }

  return paint;
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

std::tuple<bool, Font> SVGRenderContext::resolveFont() const {
  // Since there may be SVGs without any text, we accept the absence of a font manager.
  // However, this will result in the inability to render text.
  if (!fontManager) {
    return {false, Font()};
  }
  const auto& family = _presentationContext->_inherited.FontFamily->family();
  auto fontWeight = _presentationContext->_inherited.FontWeight->type();
  auto fontStyle = _presentationContext->_inherited.FontStyle->type();

  SVGFontInfo info(fontWeight, fontStyle);
  auto typeface = fontManager->getTypefaceForRendering(family, info);
  if (!typeface) {
    return {false, Font()};
  }

  float size = _lengthContext->resolve(_presentationContext->_inherited.FontSize->size(),
                                       SVGLengthContext::LengthType::Vertical);
  return {true, Font(typeface, size)};
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