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

#include "tgfx/svg/SVGDOM.h"
#include <cstddef>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include "core/utils/Log.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGLengthContext.h"
#include "svg/SVGNodeConstructor.h"
#include "svg/SVGRenderContext.h"
#include "svg/SystemFontManager.h"
#include "svg/SystemResourceLoader.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Surface.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

std::shared_ptr<SVGDOM> SVGDOM::Make(Stream& stream, SVGDOMOptions options) {
  // Parse the data into an XML DOM structure
  auto xmlDom = DOM::Make(stream);
  if (!xmlDom) {
    return nullptr;
  }

  // Convert the XML structure to an SVG structure, translating XML elements and attributes into
  // SVG elements and attributes
  SVGIDMapper mapper;
  ConstructionContext constructionContext(&mapper);
  auto root =
      SVGNodeConstructor::ConstructSVGNode(constructionContext, xmlDom->getRootNode().get());
  if (!root || root->tag() != SVGTag::Svg) {
    return nullptr;
  }

  // Create SVGDOM with the root node and ID mapper
  return std::shared_ptr<SVGDOM>(
      new SVGDOM(std::static_pointer_cast<SVGRoot>(root), std::move(options), std::move(mapper)));
}

SVGDOM::SVGDOM(std::shared_ptr<SVGRoot> root, SVGDOMOptions options, SVGIDMapper&& mapper)
    : root(std::move(root)), nodeIDMapper(std::move(mapper)), options(std::move(options)) {
}

const std::shared_ptr<SVGRoot>& SVGDOM::getRoot() const {
  return root;
}

void SVGDOM::render(Canvas* canvas) {
  // If the container size is not set, use the size of the root SVG element.
  auto drawSize = containerSize;
  if (drawSize.isEmpty()) {
    auto rootWidth = root->getWidth();
    auto rootHeight = root->getHeight();
    if (root->getViewBox().has_value()) {
      SVGLengthContext viewportLengthContext(root->getViewBox()->size());
      drawSize = Size::Make(
          viewportLengthContext.resolve(rootWidth, SVGLengthContext::LengthType::Horizontal),
          viewportLengthContext.resolve(rootHeight, SVGLengthContext::LengthType::Vertical));
    } else {
      drawSize = Size::Make(rootWidth.value(), rootHeight.value());
    }
  }
  if (!canvas || !root || drawSize.isEmpty()) {
    return;
  }

  SVGLengthContext lengthContext(drawSize);
  SVGPresentationContext presentationContext;

  auto resourceLoader =
      options.resourceProvider ? options.resourceProvider : SystemResourceLoader::Make();
  auto fontManager = options.fontManager ? options.fontManager : SystemFontManager::Make();

  SVGRenderContext renderContext(canvas, fontManager, resourceLoader, nodeIDMapper, lengthContext,
                                 presentationContext, {nullptr, nullptr}, canvas->getMatrix());

  root->render(renderContext);
}

const Size& SVGDOM::getContainerSize() const {
  return containerSize;
}

void SVGDOM::setContainerSize(const Size& size) {
  containerSize = size;
}

}  // namespace tgfx