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
#include "tgfx/core/FontManager.h"
#include "tgfx/core/LoadResourceProvider.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/svg/node/SVGSVG.h"
#include "tgfx/svg/shaper/ShaperFactory.h"

namespace tgfx {

class SVGNode;
using SVGIDMapper = std::unordered_map<std::string, std::shared_ptr<SVGNode>>;

/**
 * Options for rendering an SVGDOM object. Users can customize fonts, image resources, and shape 
 * text. Implementations should be based on the abstract class interfaces.
 */
struct SVGDOMOptions {
  /**
   * If fontManager is null, text will not be rendered.
   */
  std::shared_ptr<FontManager> fontManager = nullptr;
  /**
   * If resourceProvider is null, base64 image resources will still be parsed, but non-base64 images 
   * will be loaded from absolute paths.
   */
  std::shared_ptr<LoadResourceProvider> resourceProvider = nullptr;
  /**
   * If shaperFactory is null, right-to-left and language-specific text shaping will not be applied.
   */
  std::shared_ptr<shapers::Factory> shaperFactory = nullptr;
};

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
   */
  static std::shared_ptr<SVGDOM> Make(Stream& stream, SVGDOMOptions options = {});

  /**
   * Returns the root SVG node.
   */
  const std::shared_ptr<SVGSVG>& getRoot() const;

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
   * Returns the size of the container that the SVG will be rendered into.
   */
  const Size& getContainerSize() const;

 private:
  /**
   * Construct a new SVGDOM object
   */
  SVGDOM(std::shared_ptr<SVGSVG> root, SVGDOMOptions options, SVGIDMapper&& mapper);

  const std::shared_ptr<SVGSVG> root = nullptr;
  const SVGIDMapper nodeIDMapper = {};
  const SVGDOMOptions options = {};
  Size containerSize = {};
};
}  // namespace tgfx