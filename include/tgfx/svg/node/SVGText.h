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
class SVGTextFragment : public SVGTransformableNode {
 public:
  void renderText(const SVGRenderContext& context, const ShapedTextCallback& function) const;

 protected:
  explicit SVGTextFragment(SVGTag t) : INHERITED(t) {
  }

  virtual void onShapeText(const SVGRenderContext& context,
                           const ShapedTextCallback& function) const = 0;

  // Text nodes other than the root <text> element are not rendered directly.
  void onRender(const SVGRenderContext& /*context*/) const override {
  }

 private:
  Path onAsPath(const SVGRenderContext& context) const override;

  using INHERITED = SVGTransformableNode;
};

// Base class for nestable text containers (<text>, <tspan>, etc).
class SVGTextContainer : public SVGTextFragment {
 public:
  SVG_ATTR(X, std::vector<SVGLength>, {})
  SVG_ATTR(Y, std::vector<SVGLength>, {})
  SVG_ATTR(Dx, std::vector<SVGLength>, {})
  SVG_ATTR(Dy, std::vector<SVGLength>, {})
  SVG_ATTR(Rotate, std::vector<SVGNumberType>, {})

  void appendChild(std::shared_ptr<SVGNode> child) final;
  const std::vector<std::shared_ptr<SVGTextFragment>>& getTextChildren() const;

 protected:
  explicit SVGTextContainer(SVGTag t) : INHERITED(t) {
  }

  void onShapeText(const SVGRenderContext& context,
                   const ShapedTextCallback& function) const override;

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  std::vector<std::shared_ptr<SVGTextFragment>> children;

  using INHERITED = SVGTextFragment;
};

class SVGText final : public SVGTextContainer {
 public:
  static std::shared_ptr<SVGText> Make() {
    return std::shared_ptr<SVGText>(new SVGText());
  }

 private:
  SVGText() : INHERITED(SVGTag::Text) {
  }

  void onRender(const SVGRenderContext& context) const override;
  Rect onObjectBoundingBox(const SVGRenderContext& context) const override;
  Path onAsPath(const SVGRenderContext& context) const override;

  using INHERITED = SVGTextContainer;
};

class SVGTSpan final : public SVGTextContainer {
 public:
  static std::shared_ptr<SVGTSpan> Make() {
    return std::shared_ptr<SVGTSpan>(new SVGTSpan());
  }

 private:
  SVGTSpan() : INHERITED(SVGTag::TSpan) {
  }

  using INHERITED = SVGTextContainer;
};

class SVGTextLiteral final : public SVGTextFragment {
 public:
  static std::shared_ptr<SVGTextLiteral> Make() {
    return std::shared_ptr<SVGTextLiteral>(new SVGTextLiteral());
  }

  SVG_ATTR(Text, SVGStringType, SVGStringType())

 private:
  SVGTextLiteral() : INHERITED(SVGTag::TextLiteral) {
  }

  void onShapeText(const SVGRenderContext& context,
                   const ShapedTextCallback& function) const override;

  void appendChild(std::shared_ptr<SVGNode> /*node*/) override {
  }

  using INHERITED = SVGTextFragment;
};

class SVGTextPath final : public SVGTextContainer {
 public:
  static std::shared_ptr<SVGTextPath> Make() {
    return std::shared_ptr<SVGTextPath>(new SVGTextPath());
  }

  SVG_ATTR(Href, SVGIRI, {})
  SVG_ATTR(StartOffset, SVGLength, SVGLength(0))

 private:
  SVGTextPath() : INHERITED(SVGTag::TextPath) {
  }

  void onShapeText(const SVGRenderContext& context,
                   const ShapedTextCallback& function) const override;

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  using INHERITED = SVGTextContainer;
};
}  // namespace tgfx