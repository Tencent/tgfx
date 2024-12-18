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

#pragma once

#include <memory>
#include <utility>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SVGValue;
class SVGRenderContext;

enum class SVGTag {
  Circle,
  ClipPath,
  Defs,
  Ellipse,
  FeBlend,
  FeColorMatrix,
  FeComponentTransfer,
  FeComposite,
  FeDiffuseLighting,
  FeDisplacementMap,
  FeDistantLight,
  FeFlood,
  FeFuncA,
  FeFuncR,
  FeFuncG,
  FeFuncB,
  FeGaussianBlur,
  FeImage,
  FeMerge,
  FeMergeNode,
  FeMorphology,
  FeOffset,
  FePointLight,
  FeSpecularLighting,
  FeSpotLight,
  FeTurbulence,
  Filter,
  G,
  Image,
  Line,
  LinearGradient,
  Mask,
  Path,
  Pattern,
  Polygon,
  Polyline,
  RadialGradient,
  Rect,
  Stop,
  Svg,
  Text,
  TextLiteral,
  TextPath,
  TSpan,
  Use
};

#define SVG_PRES_ATTR(attr_name, attr_type, attr_inherited)                           \
 private:                                                                             \
  bool set##attr_name(std::optional<SVGProperty<attr_type, (attr_inherited)>>&& pr) { \
    if (pr.has_value()) {                                                             \
      this->set##attr_name(std::move(*pr));                                           \
    }                                                                                 \
    return pr.has_value();                                                            \
  }                                                                                   \
                                                                                      \
 public:                                                                              \
  const SVGProperty<attr_type, attr_inherited>& get##attr_name() const {              \
    return presentationAttributes.attr_name;                                          \
  }                                                                                   \
  void set##attr_name(const SVGProperty<attr_type, attr_inherited>& v) {              \
    auto* dest = &presentationAttributes.attr_name;                                   \
    if (!dest->isInheritable() || v.isValue()) {                                      \
      /* TODO: If dest is not inheritable, handle v == "inherit" */                   \
      *dest = v;                                                                      \
    } else {                                                                          \
      dest->set(SVGPropertyState::Inherit);                                           \
    }                                                                                 \
  }                                                                                   \
  void set##attr_name(SVGProperty<attr_type, attr_inherited>&& v) {                   \
    auto* dest = &presentationAttributes.attr_name;                                   \
    if (!dest->isInheritable() || v.isValue()) {                                      \
      /* TODO: If dest is not inheritable, handle v == "inherit" */                   \
      *dest = std::move(v);                                                           \
    } else {                                                                          \
      dest->set(SVGPropertyState::Inherit);                                           \
    }                                                                                 \
  }

class SVGNode {
 public:
  virtual ~SVGNode();

  SVGTag tag() const {
    return _tag;
  }

  SVG_PRES_ATTR(ClipRule, SVGFillRule, true)
  SVG_PRES_ATTR(Color, SVGColorType, true)
  SVG_PRES_ATTR(ColorInterpolation, SVGColorspace, true)
  SVG_PRES_ATTR(ColorInterpolationFilters, SVGColorspace, true)
  SVG_PRES_ATTR(FillRule, SVGFillRule, true)
  SVG_PRES_ATTR(Fill, SVGPaint, true)
  SVG_PRES_ATTR(FillOpacity, SVGNumberType, true)
  SVG_PRES_ATTR(FontFamily, SVGFontFamily, true)
  SVG_PRES_ATTR(FontSize, SVGFontSize, true)
  SVG_PRES_ATTR(FontStyle, SVGFontStyle, true)
  SVG_PRES_ATTR(FontWeight, SVGFontWeight, true)
  SVG_PRES_ATTR(Stroke, SVGPaint, true)
  SVG_PRES_ATTR(StrokeDashArray, SVGDashArray, true)
  SVG_PRES_ATTR(StrokeDashOffset, SVGLength, true)
  SVG_PRES_ATTR(StrokeLineCap, SVGLineCap, true)
  SVG_PRES_ATTR(StrokeLineJoin, SVGLineJoin, true)
  SVG_PRES_ATTR(StrokeMiterLimit, SVGNumberType, true)
  SVG_PRES_ATTR(StrokeOpacity, SVGNumberType, true)
  SVG_PRES_ATTR(StrokeWidth, SVGLength, true)
  SVG_PRES_ATTR(TextAnchor, SVGTextAnchor, true)
  SVG_PRES_ATTR(Visibility, SVGVisibility, true)

  // not inherited
  SVG_PRES_ATTR(ClipPath, SVGFuncIRI, false)
  SVG_PRES_ATTR(Display, SVGDisplay, false)
  SVG_PRES_ATTR(Mask, SVGFuncIRI, false)
  SVG_PRES_ATTR(Filter, SVGFuncIRI, false)
  SVG_PRES_ATTR(Opacity, SVGNumberType, false)
  SVG_PRES_ATTR(StopColor, SVGColor, false)
  SVG_PRES_ATTR(StopOpacity, SVGNumberType, false)
  SVG_PRES_ATTR(FloodColor, SVGColor, false)
  SVG_PRES_ATTR(FloodOpacity, SVGNumberType, false)
  SVG_PRES_ATTR(LightingColor, SVGColor, false)

  void render(const SVGRenderContext&) const;

  bool asPaint(const SVGRenderContext&, Paint*) const;

  Path asPath(const SVGRenderContext&) const;

  Rect objectBoundingBox(const SVGRenderContext&) const;

  virtual bool hasChildren() const {
    return false;
  }

 protected:
  explicit SVGNode(SVGTag);

  virtual void appendChild(std::shared_ptr<SVGNode>) = 0;

  void setAttribute(SVGAttribute, const SVGValue&);

  bool setAttribute(const std::string& attributeName, const std::string& attributeValue);

  virtual bool parseAndSetAttribute(const std::string& name, const std::string& value);

  static Matrix ComputeViewboxMatrix(const Rect&, const Rect&, SVGPreserveAspectRatio);

  virtual void onSetAttribute(SVGAttribute, const SVGValue&){};

  // Called before onRender(), to apply local attributes to the context.  Unlike onRender(),
  // onPrepareToRender() bubbles up the inheritance chain: override should always call
  // INHERITED::onPrepareToRender(), unless they intend to short-circuit rendering
  // (return false).
  // Implementations are expected to return true if rendering is to continue, or false if
  // the node/subtree rendering is disabled.
  virtual bool onPrepareToRender(SVGRenderContext*) const;

  virtual void onRender(const SVGRenderContext&) const = 0;

  virtual bool onAsPaint(const SVGRenderContext&, Paint*) const {
    return false;
  }

  virtual Path onAsPath(const SVGRenderContext&) const = 0;

  virtual Rect onObjectBoundingBox(const SVGRenderContext&) const {
    return Rect::MakeEmpty();
  }

 private:
  SVGTag _tag;
  SVGPresentationAttributes presentationAttributes;

  friend class SVGNodeConstructor;
  friend class SVGRenderContext;
};

//NOLINTBEGIN
#undef SVG_PRES_ATTR  // presentation attributes are only defined for the base class

#define SVG_ATTR_SETTERS(attr_name, attr_type, attr_default, set_cp, set_mv) \
 private:                                                                    \
  bool set##attr_name(const std::optional<attr_type>& pr) {                  \
    if (pr.has_value()) {                                                    \
      this->set##attr_name(*pr);                                             \
    }                                                                        \
    return pr.has_value();                                                   \
  }                                                                          \
  bool set##attr_name(std::optional<attr_type>&& pr) {                       \
    if (pr.has_value()) {                                                    \
      this->set##attr_name(std::move(*pr));                                  \
    }                                                                        \
    return pr.has_value();                                                   \
  }                                                                          \
                                                                             \
 public:                                                                     \
  void set##attr_name(const attr_type& a) {                                  \
    set_cp(a);                                                               \
  }                                                                          \
  void set##attr_name(attr_type&& a) {                                       \
    set_mv(std::move(a));                                                    \
  }

#define SVG_ATTR(attr_name, attr_type, attr_default)                                           \
 private:                                                                                      \
  attr_type attr_name = attr_default;                                                          \
                                                                                               \
 public:                                                                                       \
  const attr_type& get##attr_name() const {                                                    \
    return attr_name;                                                                          \
  }                                                                                            \
  SVG_ATTR_SETTERS(                                                                            \
      attr_name, attr_type, attr_default, [this](const attr_type& a) { this->attr_name = a; }, \
      [this](attr_type&& a) { this->attr_name = std::move(a); })

#define SVG_OPTIONAL_ATTR(attr_name, attr_type)                                                \
 private:                                                                                      \
  std::optional<attr_type> attr_name;                                                          \
                                                                                               \
 public:                                                                                       \
  const std::optional<attr_type>& get##attr_name() const {                                     \
    return attr_name;                                                                          \
  }                                                                                            \
  SVG_ATTR_SETTERS(                                                                            \
      attr_name, attr_type, attr_default, [this](const attr_type& a) { this->attr_name = a; }, \
      [this](attr_type&& a) { this->attr_name = a; })
//NOLINTEND

}  // namespace tgfx