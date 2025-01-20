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
#include "svg/SVGNodeConstructor.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Surface.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGFontManager.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

std::shared_ptr<SVGDOM> SVGDOM::Make(const std::shared_ptr<Data>& data) {
  if (!data) {
    return nullptr;
  }
  // Parse the data into an XML DOM structure
  auto xmlDom = DOM::MakeFromData(*data);
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
      new SVGDOM(std::static_pointer_cast<SVGSVG>(root), std::move(mapper)));
}

SVGDOM::SVGDOM(std::shared_ptr<SVGSVG> root, SVGIDMapper&& mapper)
    : root(std::move(root)), nodeIDMapper(std::move(mapper)) {
}

const std::shared_ptr<SVGSVG>& SVGDOM::getRoot() const {
  return root;
}

void SVGDOM::collectRenderFonts(const std::shared_ptr<SVGFontManager>& fontManager) {
  if (!root || !fontManager) {
    return;
  }

  auto fontCollector = [](auto collector, const std::shared_ptr<SVGNode>& node,
                          const std::shared_ptr<SVGFontManager>& fontManager) -> void {
    if (!node) {
      return;
    }
    if (node->tag() >= SVGTag::Text && node->tag() <= SVGTag::TSpan) {
      if (node->getFontFamily().isValue()) {
        auto family = node->getFontFamily()->family();
        SVGFontWeight::Type weight = node->getFontWeight().isValue() ? node->getFontWeight()->type()
                                                                     : SVGFontWeight::Type::Normal;
        SVGFontStyle::Type style = node->getFontStyle().isValue() ? node->getFontStyle()->type()
                                                                  : SVGFontStyle::Type::Normal;
        SVGFontInfo fontStyle(weight, style);
        fontManager->addFontStyle(family, fontStyle);
      }
    } else if (node->hasChildren()) {
      if (auto container = std::static_pointer_cast<SVGContainer>(node)) {
        for (const auto& child : container->getChildren()) {
          collector(collector, child, fontManager);
        }
      }
    }
  };
  fontCollector(fontCollector, std::static_pointer_cast<SVGContainer>(root), fontManager);
}

void SVGDOM::render(Canvas* canvas, const std::shared_ptr<SVGFontManager>& fontManager) {
  // If the container size is not set, use the size of the root SVG element.
  auto drawSize = containerSize;
  if (drawSize.isEmpty()) {
    if (root->getViewBox().has_value()) {
      drawSize = root->getViewBox()->size();
    } else {
      drawSize = Size::Make(root->getWidth().value(), root->getHeight().value());
    }
  }
  if (!canvas || !root || drawSize.isEmpty()) {
    return;
  }

  SVGLengthContext lengthContext(containerSize);
  SVGPresentationContext presentationContext;
  SVGRenderContext renderContext(canvas, fontManager, nodeIDMapper, lengthContext,
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