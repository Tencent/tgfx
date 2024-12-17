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
#include "tgfx/core/Surface.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

std::shared_ptr<SVGDOM> SVGDOM::Make(const std::shared_ptr<Data>& data,
                                     std::shared_ptr<SVGFontManager> fontManager) {
  if (!data) {
    return nullptr;
  }
  auto xmlDom = DOM::MakeFromData(*data);
  if (!xmlDom) {
    return nullptr;
  }

  SVGIDMapper mapper;
  ConstructionContext constructionContext(&mapper);

  auto root =
      SVGNodeConstructor::ConstructSVGNode(constructionContext, xmlDom->getRootNode().get());
  if (!root || root->tag() != SVGTag::Svg) {
    return nullptr;
  }

  return std::shared_ptr<SVGDOM>(new SVGDOM(std::static_pointer_cast<SVGSVG>(root),
                                            std::move(mapper), std::move(fontManager)));
}

SVGDOM::SVGDOM(std::shared_ptr<SVGSVG> root, SVGIDMapper&& mapper,
               std::shared_ptr<SVGFontManager> fontManager)
    : root(std::move(root)), fontManager(std::move(fontManager)), _nodeIDMapper(std::move(mapper)) {
}

void SVGDOM::render(Canvas* canvas) {
  if (root) {
    if (!renderPicture) {
      SVGLengthContext lengthContext(containerSize);
      SVGPresentationContext presentationContext;

      Recorder recorder;
      auto* drawCanvas = recorder.beginRecording();
      {
        SVGRenderContext renderCtx(canvas->getSurface()->getContext(), drawCanvas, fontManager,
                                   _nodeIDMapper, lengthContext, presentationContext,
                                   {nullptr, nullptr}, canvas->getMatrix());
        root->render(renderCtx);
      }
      renderPicture = recorder.finishRecordingAsPicture();
    }
    canvas->drawPicture(renderPicture);
  }
}

const Size& SVGDOM::getContainerSize() const {
  return containerSize;
}

void SVGDOM::setContainerSize(const Size& size) {
  containerSize = size;
}

bool SVGNode::setAttribute(const std::string& attributeName, const std::string& attributeValue) {
  return SVGNodeConstructor::SetAttribute(*this, attributeName, attributeValue);
}
}  // namespace tgfx