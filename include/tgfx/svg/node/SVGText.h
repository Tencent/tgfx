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
#include <vector>
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGTransformableNode.h"

namespace tgfx {

class SVGRenderContext;

using ShapedTextCallback =
    std::function<void(const SVGRenderContext&, const std::shared_ptr<TextBlob>&)>;

// Base class for text-rendering nodes.
class SkSVGTextFragment : public SkSVGTransformableNode {
 public:
  void renderText(const SVGRenderContext&, const ShapedTextCallback&) const;

 protected:
  explicit SkSVGTextFragment(SVGTag t) : INHERITED(t) {
  }

  virtual void onShapeText(const SVGRenderContext&, const ShapedTextCallback&) const = 0;

  // Text nodes other than the root <text> element are not rendered directly.
  void onRender(const SVGRenderContext&) const override {
  }

 private:
  Path onAsPath(const SVGRenderContext&) const override;

  using INHERITED = SkSVGTransformableNode;
};

// Base class for nestable text containers (<text>, <tspan>, etc).
class SkSVGTextContainer : public SkSVGTextFragment {
 public:
  SVG_ATTR(X, std::vector<SVGLength>, {})
  SVG_ATTR(Y, std::vector<SVGLength>, {})
  SVG_ATTR(Dx, std::vector<SVGLength>, {})
  SVG_ATTR(Dy, std::vector<SVGLength>, {})
  SVG_ATTR(Rotate, std::vector<SVGNumberType>, {})

  void appendChild(std::shared_ptr<SVGNode>) final;

 protected:
  explicit SkSVGTextContainer(SVGTag t) : INHERITED(t) {
  }

  void onShapeText(const SVGRenderContext&, const ShapedTextCallback&) const override;

  bool parseAndSetAttribute(const char*, const char*) override;

 private:
  std::vector<std::shared_ptr<SkSVGTextFragment>> fChildren;

  using INHERITED = SkSVGTextFragment;
};

class SkSVGText final : public SkSVGTextContainer {
 public:
  static std::shared_ptr<SkSVGText> Make() {
    return std::shared_ptr<SkSVGText>(new SkSVGText());
  }

 private:
  SkSVGText() : INHERITED(SVGTag::Text) {
  }

  void onRender(const SVGRenderContext&) const override;
  Rect onObjectBoundingBox(const SVGRenderContext&) const override;
  Path onAsPath(const SVGRenderContext&) const override;

  using INHERITED = SkSVGTextContainer;
};

class SkSVGTSpan final : public SkSVGTextContainer {
 public:
  static std::shared_ptr<SkSVGTSpan> Make() {
    return std::shared_ptr<SkSVGTSpan>(new SkSVGTSpan());
  }

 private:
  SkSVGTSpan() : INHERITED(SVGTag::TSpan) {
  }

  using INHERITED = SkSVGTextContainer;
};

class SkSVGTextLiteral final : public SkSVGTextFragment {
 public:
  static std::shared_ptr<SkSVGTextLiteral> Make() {
    return std::shared_ptr<SkSVGTextLiteral>(new SkSVGTextLiteral());
  }

  SVG_ATTR(Text, SVGStringType, SVGStringType())

 private:
  SkSVGTextLiteral() : INHERITED(SVGTag::TextLiteral) {
  }

  void onShapeText(const SVGRenderContext&, const ShapedTextCallback&) const override;

  void appendChild(std::shared_ptr<SVGNode>) override {
  }

  using INHERITED = SkSVGTextFragment;
};

class SkSVGTextPath final : public SkSVGTextContainer {
 public:
  static std::shared_ptr<SkSVGTextPath> Make() {
    return std::shared_ptr<SkSVGTextPath>(new SkSVGTextPath());
  }

  SVG_ATTR(Href, SVGIRI, {})
  SVG_ATTR(StartOffset, SVGLength, SVGLength(0))

 private:
  SkSVGTextPath() : INHERITED(SVGTag::TextPath) {
  }

  void onShapeText(const SVGRenderContext&, const ShapedTextCallback&) const override;

  bool parseAndSetAttribute(const char*, const char*) override;

  using INHERITED = SkSVGTextContainer;
};
}  // namespace tgfx