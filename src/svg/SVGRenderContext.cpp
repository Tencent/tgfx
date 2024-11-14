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
#include <memory>
#include <optional>
#include "core/utils/Log.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/svg/node/SVGClipPath.h"
#include "tgfx/svg/node/SVGFilter.h"
#include "tgfx/svg/node/SVGMask.h"
#include "tgfx/svg/node/SVGNode.h"

#ifndef RENDER_SVG

namespace tgfx {

namespace {

float length_size_for_type(const Size& viewport, SVGLengthContext::LengthType t) {
  switch (t) {
    case SVGLengthContext::LengthType::kHorizontal:
      return viewport.width;
    case SVGLengthContext::LengthType::kVertical:
      return viewport.height;
    case SVGLengthContext::LengthType::kOther: {
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
      if (fPatternUnit.has_value()) {
        if (fPatternUnit.value() == SVGPatternUnits::ObjectBoundingBox) {
          return l.value() * length_size_for_type(fViewport, t);
        } else {
          return l.value();
        }
      } else {
        return l.value();
      }
    }
    case SVGLength::Unit::kPX:
      return l.value();
    case SVGLength::Unit::kPercentage: {
      if (fPatternUnit.has_value()) {
        if (fPatternUnit.value() == SVGPatternUnits::ObjectBoundingBox) {
          return l.value() * length_size_for_type(fViewport, t) / 100.f;
        } else {
          return l.value() / 100.f;
        }
      } else {
        return l.value() * length_size_for_type(fViewport, t) / 100;
      }
    }
    case SVGLength::Unit::kCM:
      return l.value() * fDPI * kCMMultiplier;
    case SVGLength::Unit::kMM:
      return l.value() * fDPI * kMMMultiplier;
    case SVGLength::Unit::kIN:
      return l.value() * fDPI * kINMultiplier;
    case SVGLength::Unit::kPT:
      return l.value() * fDPI * kPTMultiplier;
    case SVGLength::Unit::kPC:
      return l.value() * fDPI * kPCMultiplier;
    default:
      //unsupported unit type
      ASSERT(false);
      return 0;
  }
}

Rect SVGLengthContext::resolveRect(const SVGLength& x, const SVGLength& y, const SVGLength& width,
                                   const SVGLength& height) const {
  return Rect::MakeXYWH(this->resolve(x, SVGLengthContext::LengthType::kHorizontal),
                        this->resolve(y, SVGLengthContext::LengthType::kVertical),
                        this->resolve(width, SVGLengthContext::LengthType::kHorizontal),
                        this->resolve(height, SVGLengthContext::LengthType::kVertical));
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

std::unique_ptr<PathEffect> dash_effect(const SkSVGPresentationAttributes& props,
                                        const SVGLengthContext& lctx) {
  if (props.fStrokeDashArray->type() != SVGDashArray::Type::kDashArray) {
    return nullptr;
  }

  const auto& da = *props.fStrokeDashArray;
  const auto count = da.dashArray().size();
  std::vector<float> intervals;
  intervals.reserve(count);
  for (const auto& dash : da.dashArray()) {
    intervals.push_back(lctx.resolve(dash, SVGLengthContext::LengthType::kOther));
  }

  if (count & 1) {
    // If an odd number of values is provided, then the list of values
    // is repeated to yield an even number of values.
    intervals.resize(count * 2);
    memcpy(intervals.data() + count, intervals.data(), count * sizeof(float));
  }

  ASSERT((intervals.size() & 1) == 0);

  const auto phase = lctx.resolve(*props.fStrokeDashOffset, SVGLengthContext::LengthType::kOther);

  return PathEffect::MakeDash(intervals.data(), static_cast<int>(intervals.size()), phase);
}

}  // namespace

SkSVGPresentationContext::SkSVGPresentationContext()
    : fInherited(SkSVGPresentationAttributes::MakeInitial()) {
}

SVGRenderContext::SVGRenderContext(Canvas* canvas,
                                   const std::shared_ptr<SVGFontManager>& fontManager,
                                   const std::shared_ptr<ResourceProvider>& resource,
                                   const SVGIDMapper& mapper, const SVGLengthContext& lctx,
                                   const SkSVGPresentationContext& pctx, const OBBScope& obbs)
    : fFontMgr(fontManager), fResourceProvider(resource), fIDMapper(mapper), fLengthContext(lctx),
      fPresentationContext(pctx), fCanvas(canvas), fCanvasSaveCount(canvas->getSaveCount()),
      fOBBScope(obbs) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other)
    : SVGRenderContext(other.fCanvas, other.fFontMgr, other.fResourceProvider, other.fIDMapper,
                       *other.fLengthContext, *other.fPresentationContext, other.fOBBScope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas)
    : SVGRenderContext(canvas, other.fFontMgr, other.fResourceProvider, other.fIDMapper,
                       *other.fLengthContext, *other.fPresentationContext, other.fOBBScope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, const SVGLengthContext& lengthCtx)
    : SVGRenderContext(other.fCanvas, other.fFontMgr, other.fResourceProvider, other.fIDMapper,
                       lengthCtx, *other.fPresentationContext, other.fOBBScope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, Canvas* canvas,
                                   const SVGLengthContext& lengthCtx)
    : SVGRenderContext(canvas, other.fFontMgr, other.fResourceProvider, other.fIDMapper, lengthCtx,
                       *other.fPresentationContext, other.fOBBScope) {
}

SVGRenderContext::SVGRenderContext(const SVGRenderContext& other, const SVGNode* node)
    : SVGRenderContext(other.fCanvas, other.fFontMgr, other.fResourceProvider, other.fIDMapper,
                       *other.fLengthContext, *other.fPresentationContext, OBBScope{node, this}) {
}

SVGRenderContext::~SVGRenderContext() {
  fCanvas->restoreToCount(fCanvasSaveCount);
}

std::shared_ptr<SVGNode> SVGRenderContext::findNodeById(const SVGIRI& iri) const {
  if (iri.type() != SVGIRI::Type::kLocal) {
    return nullptr;
  }
  auto p = fIDMapper.find(iri.iri())->second;
  return p;
}

void SVGRenderContext::applyPresentationAttributes(const SkSVGPresentationAttributes& attrs,
                                                   uint32_t flags) {

#define ApplyLazyInheritedAttribute(ATTR)                                       \
  do {                                                                          \
    /* All attributes should be defined on the inherited context. */            \
    ASSERT(fPresentationContext->fInherited.f##ATTR.isValue());                 \
    const auto& attr = attrs.f##ATTR;                                           \
    if (attr.isValue() && *attr != *fPresentationContext->fInherited.f##ATTR) { \
      /* Update the local attribute value */                                    \
      fPresentationContext.writable()->fInherited.f##ATTR.set(*attr);           \
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

  // Uninherited attributes.  Only apply to the current context.

  const bool hasFilter = attrs.fFilter.isValue();
  if (attrs.fOpacity.isValue()) {
    this->applyOpacity(*attrs.fOpacity, flags, hasFilter);
  }

  if (attrs.fClipPath.isValue()) {
    this->applyClip(*attrs.fClipPath);
  }

  if (attrs.fMask.isValue()) {
    this->applyMask(*attrs.fMask);
  }

  // TODO: when both a filter and opacity are present, we can apply both with a single layer
  if (hasFilter) {
    this->applyFilter(*attrs.fFilter);
  }

  // Remaining uninherited presentation attributes are accessed as SkSVGNode fields, not via
  // the render context.
  // TODO: resolve these in a pre-render styling pass and assert here that they are values.
  // - stop-color
  // - stop-opacity
  // - flood-color
  // - flood-opacity
  // - lighting-color
}

void SVGRenderContext::applyOpacity(float opacity, uint32_t flags, bool hasFilter) {
  if (opacity >= 1) {
    return;
  }

  const auto& props = fPresentationContext->fInherited;
  const bool hasFill = props.fFill->type() != SVGPaint::Type::kNone;
  const bool hasStroke = props.fStroke->type() != SVGPaint::Type::kNone;

  // We can apply the opacity as paint alpha if it only affects one atomic draw.
  // For now, this means all of the following must be true:
  //   - the target node doesn't have any descendants;
  //   - it only has a stroke or a fill (but not both);
  //   - it does not have a filter.
  // Going forward, we may needto refine this heuristic (e.g. to accommodate markers).
  if ((flags & kLeaf) && (hasFill ^ hasStroke) && !hasFilter) {
    fDeferredPaintOpacity *= opacity;
  } else {
    // Expensive, layer-based fall back.
    Paint opacityPaint;
    opacityPaint.setAlpha(std::clamp(opacity, 0.0f, 1.0f));
    // Balanced in the destructor, via restoreToCount().

    //TODO (YG)
    // fCanvas.saveLayer(nullptr, &opacityPaint);
  }
}

void SVGRenderContext::applyFilter(const SVGFuncIRI& filter) {
  if (filter.type() != SVGFuncIRI::Type::kIRI) {
    return;
  }

  const auto node = this->findNodeById(filter.iri());
  if (!node || node->tag() != SVGTag::kFilter) {
    return;
  }

  const auto* filterNode = reinterpret_cast<const SkSVGFilter*>(node.get());
  auto imageFilter = filterNode->buildFilterDAG(*this);
  if (imageFilter) {
    Paint filterPaint;
    filterPaint.setImageFilter(imageFilter);
    // Balanced in the destructor, via restoreToCount().
    //TODO (YG)
    // fCanvas->saveLayer(nullptr, &filterPaint);
  }
}

void SVGRenderContext::saveOnce() {
  // The canvas only needs to be saved once, per local SVGRenderContext.
  if (fCanvas->getSaveCount() == fCanvasSaveCount) {
    fCanvas->save();
  }
  ASSERT(fCanvas->getSaveCount() > fCanvasSaveCount);
}

void SVGRenderContext::applyClip(const SVGFuncIRI& clip) {
  if (clip.type() != SVGFuncIRI::Type::kIRI) {
    return;
  }

  const auto clipNode = this->findNodeById(clip.iri());
  if (!clipNode || clipNode->tag() != SVGTag::kClipPath) {
    return;
  }

  const Path clipPath = static_cast<const SVGClipPath*>(clipNode.get())->resolveClip(*this);

  // We use the computed clip path in two ways:
  //
  //   - apply to the current canvas, for drawing
  //   - track in the presentation context, for asPath() composition
  //
  // TODO: the two uses are exclusive, avoid canvas churn when non needed.

  this->saveOnce();

  fCanvas->clipPath(clipPath);
  fClipPath = clipPath;
}

void SVGRenderContext::applyMask(const SVGFuncIRI& mask) {
  if (mask.type() != SVGFuncIRI::Type::kIRI) {
    return;
  }

  const auto node = this->findNodeById(mask.iri());
  if (!node || node->tag() != SVGTag::kMask) {
    return;
  }

  const auto* mask_node = static_cast<const SkSVGMask*>(node.get());
  const auto mask_bounds = mask_node->bounds(*this);

  // Isolation/mask layer.
  // TODO (YG)
  // fCanvas->saveLayer(mask_bounds, nullptr);

  // Render and filter mask content.
  mask_node->renderMask(*this);

  // Content layer
  Paint masking_paint;
  masking_paint.setBlendMode(BlendMode::SrcIn);

  // TODO (YG)
  // fCanvas->saveLayer(mask_bounds, &masking_paint);

  // Content is also clipped to the specified mask bounds.
  fCanvas->clipRect(mask_bounds);

  // At this point we're set up for content rendering.
  // The pending layers are restored in the destructor (render context scope exit).
  // Restoring triggers srcIn-compositing the content against the mask.
}

std::optional<Paint> SVGRenderContext::commonPaint(const SVGPaint& paint_selector,
                                                   float paint_opacity) const {
  if (paint_selector.type() == SVGPaint::Type::kNone) {
    return std::optional<Paint>();
  }

  std::optional<Paint> p = Paint();
  p->getAlpha();
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
      pctx.fNamedColors = fPresentationContext->fNamedColors;
      SVGRenderContext local_ctx(fCanvas, fFontMgr, fResourceProvider, fIDMapper, *fLengthContext,
                                 pctx, fOBBScope);

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
  p->setAlpha(std::clamp(p->getAlpha() * paint_opacity * fDeferredPaintOpacity, 0.0f, 1.0f));
  return p;
}

std::optional<Paint> SVGRenderContext::fillPaint() const {
  const auto& props = fPresentationContext->fInherited;
  auto p = this->commonPaint(*props.fFill, *props.fFillOpacity);

  if (p.has_value()) {
    p->setStyle(PaintStyle::Fill);
  }

  return p;
}

std::optional<Paint> SVGRenderContext::strokePaint() const {
  const auto& props = fPresentationContext->fInherited;
  auto p = this->commonPaint(*props.fStroke, *props.fStrokeOpacity);

  if (p.has_value()) {
    p->setStyle(PaintStyle::Stroke);
    p->setStrokeWidth(
        fLengthContext->resolve(*props.fStrokeWidth, SVGLengthContext::LengthType::kOther));
    Stroke stroke;
    stroke.cap = toSkCap(*props.fStrokeLineCap);
    stroke.join = toSkJoin(*props.fStrokeLineJoin);
    stroke.miterLimit = *props.fStrokeMiterLimit;
    p->setStroke(stroke);

    //TODO (YG)
    // p->setPathEffect(dash_effect(props, *fLengthContext));
    dash_effect(props, *fLengthContext);
  }

  return p;
}

SVGColorType SVGRenderContext::resolveSvgColor(const SVGColor& color) const {
  if (fPresentationContext->fNamedColors) {
    for (auto&& ident : *color.vars()) {
      auto iter = fPresentationContext->fNamedColors->find(ident);
      if (iter != fPresentationContext->fNamedColors->end()) {
        return iter->second;
      }
    }
  }
  switch (color.type()) {
    case SVGColor::Type::kColor:
      return color.color();
    case SVGColor::Type::kCurrentColor:
      return *fPresentationContext->fInherited.fColor;
    case SVGColor::Type::kICCColor:
      return Color::Black();
  }
}

SVGRenderContext::OBBTransform SVGRenderContext::transformForCurrentOBB(
    SVGObjectBoundingBoxUnits u) const {
  if (!fOBBScope.fNode || u.type() == SVGObjectBoundingBoxUnits::Type::kUserSpaceOnUse) {
    return {{0, 0}, {1, 1}};
  }
  ASSERT(fOBBScope.fCtx);

  const auto obb = fOBBScope.fNode->objectBoundingBox(*fOBBScope.fCtx);
  return {{obb.x(), obb.y()}, {obb.width(), obb.height()}};
}

Rect SVGRenderContext::resolveOBBRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                                      const SVGLength& h, SVGObjectBoundingBoxUnits obbu) const {
  CopyOnWrite<SVGLengthContext> lctx(fLengthContext);

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