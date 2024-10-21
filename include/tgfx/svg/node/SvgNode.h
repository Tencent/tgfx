/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <string>
#include "tgfx/core/Matrix.h"
namespace tgfx {

enum class SvgTag : uint32_t {
  kCircle,
  kClipPath,
  kDefs,
  kEllipse,
  kFeBlend,
  kFeColorMatrix,
  kFeComponentTransfer,
  kFeComposite,
  kFeDiffuseLighting,
  kFeDisplacementMap,
  kFeDistantLight,
  kFeFlood,
  kFeFuncA,
  kFeFuncR,
  kFeFuncG,
  kFeFuncB,
  kFeGaussianBlur,
  kFeImage,
  kFeMerge,
  kFeMergeNode,
  kFeMorphology,
  kFeOffset,
  kFePointLight,
  kFeSpecularLighting,
  kFeSpotLight,
  kFeTurbulence,
  kFilter,
  kG,
  kImage,
  kLine,
  kLinearGradient,
  kMask,
  kPath,
  kPattern,
  kPolygon,
  kPolyline,
  kRadialGradient,
  kRect,
  kStop,
  kSvg,
  kText,
  kTextLiteral,
  kTextPath,
  kTSpan,
  kUse
};

/**
 * SvgNode represents the node base property in SVG DOM.
 */
class SvgNode {
 public:
  virtual ~SvgNode() = default;

  /**
   * Renders the SVG node to the canvas.subclasses should override this method to draw their own
   * content.if the node is a container node, it should call render() on its children.
   */
  // virtual void render(std::shared_ptr<Canvas> canvas) = 0;

  /**
   * Returns the tag of the SVG node.
   */
  SvgTag getTag() const;

  /**
   * Sets the SVG node's attribute names and values using the attributes parsed in xml. 
   * Subclasses should override this method to handle their own attributes. If the attribute
   * is wrong or does not fit the current class, false should be returned.
   */
  virtual bool setAttribute(const std::string& attributeName,
                            const std::string& attributeValue) = 0;

  /**
   * Sets the transformation matrix of the SVG node.
   */
  void setTransform(const Matrix& t);

  /**
   * Gets the transformation matrix of the SVG node.
   */
  Matrix getTransform() const;

 protected:
  explicit SvgNode(SvgTag tag);

 private:
  SvgTag _tag;
  Matrix _transform;
};
}  // namespace tgfx