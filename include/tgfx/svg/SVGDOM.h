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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGFontManager.h"
#include "tgfx/svg/node/SVGSVG.h"

namespace tgfx {

class SVGNode;
using SVGIDMapper = std::unordered_map<std::string, std::shared_ptr<SVGNode>>;

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
 *    - The simplest way to render is by calling render(canvas,nullptr). If you need to render text
 *      with specific fonts or set the size of the SVG, you can use the following methods.
 *    - If text rendering is required, use collectRenderFonts() to gather the necessary typefaces. 
 *      Traverse the typefaces collected by the fontManager and set the typeface objects.
 *    - Render the SVG using the render() method. If text rendering is needed, pass in the 
 *      fontManager object. Otherwise, you can pass in nullptr.
 *    - To set the size of the SVG, use setContainerSize(). If not set, the size of the root SVG
 *      node will be used.
 */
class SVGDOM {
 public:
  /**
   * Creates an SVGDOM object from the provided data.
   */
  static std::shared_ptr<SVGDOM> Make(const std::shared_ptr<Data>& data);

  /**
   * Returns the root SVG node.
   */
  const std::shared_ptr<SVGSVG>& getRoot() const;

  /**
   * Collects the font families and styles used for rendering and saves them in the font manager.
   */
  void collectRenderFonts(const std::shared_ptr<SVGFontManager>& fontManager);

  /**
   * Renders the SVG to the provided canvas.
   * @param canvas The canvas to render to.
   * @param fontManager The font manager for rendering SVG text. If no text rendering is needed, 
   * this can be nullptr, and text will not be rendered. If no specific font is set, the default 
   * font will be used for rendering.
   */
  void render(Canvas* canvas, const std::shared_ptr<SVGFontManager>& fontManager = nullptr);

  /**
   * Sets the size of the container that the SVG will be rendered into.
   */
  void setContainerSize(const Size& size);

  /**
   * Returns the size of the container that the SVG will be rendered into.
   */
  const Size& getContainerSize() const;

 private:
  /**
   * Construct a new SVGDOM object
   */
  SVGDOM(std::shared_ptr<SVGSVG> root, SVGIDMapper&& mapper);

  const std::shared_ptr<SVGSVG> root = nullptr;
  const SVGIDMapper nodeIDMapper = {};
  Size containerSize = {};
};
}  // namespace tgfx