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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/svg/SVGAttributeHandler.h"
#include "tgfx/svg/TextShaper.h"
#include "tgfx/svg/node/SVGRoot.h"

namespace tgfx {

class SVGNode;
using SVGIDMapper = std::unordered_map<std::string, std::shared_ptr<SVGNode>>;
using CSSMapper = std::unordered_map<std::string, std::string>;

/**
 * The SVGDOM class represents an SVG Document Object Model (DOM). It provides functionality to 
 * traverse the SVG DOM tree and render the SVG.
 * 
 * Usage:
 * 
 * 1. Traversing the SVG DOM tree:
 *    - Use getRoot() to obtain the root node. From the root node, you can access its attributes 
 *      and child nodes, and then visit the child nodes.
 *
 * 2. Rendering the SVG:
 *    - Use setContainerSize() to set the size of the canvas. If not set, the dimensions of the root
 *      node will be used.
 *    - Use render() to draw the SVG onto a canvas.
 */
class SVGDOM {
 public:
  /**
   * Creates an SVGDOM object from the provided stream.
   * If textShaper is nullptr, only text with specified system fonts will render. Text without a
   * specified font or requiring fallback fonts will not render.
   */
  static std::shared_ptr<SVGDOM> Make(Stream& stream,
                                      std::shared_ptr<TextShaper> textShaper = nullptr,
                                      std::shared_ptr<SVGParseSetter> attributeSetter = nullptr);

  /**
   * Returns the root SVG node.
   */
  const std::shared_ptr<SVGRoot>& getRoot() const;

  /**
   * Renders the SVG to the provided canvas.
   * @param canvas The canvas to render to.
   * @param fontManager The font manager for rendering SVG text. If no text rendering is needed, 
   * this can be nullptr, and text will not be rendered. If no specific font is set, the default 
   * font will be used for rendering.
   */
  void render(Canvas* canvas);

  /**
   * Sets the size of the container that the SVG will be rendered into.
   */
  void setContainerSize(const Size& size);

  /**
   * Gets the size of the container that the SVG will be rendered into. If not set, the size of the
   * root node will be used by default.
   */
  Size getContainerSize() const;

  /**
   * Returns the ID mapper for the SVG nodes.
   */
  const SVGIDMapper& nodeIDMapper() const {
    return _nodeIDMapper;
  }

 private:
  /**
   * Construct a new SVGDOM object
   */
  SVGDOM(std::shared_ptr<SVGRoot> root, std::shared_ptr<TextShaper> textShaper,
         SVGIDMapper&& mapper);

  const std::shared_ptr<SVGRoot> root = nullptr;
  const SVGIDMapper _nodeIDMapper = {};
  const std::shared_ptr<TextShaper> textShaper = nullptr;
  Size containerSize = {};
};
}  // namespace tgfx