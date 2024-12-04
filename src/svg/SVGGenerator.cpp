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

#include "tgfx/svg/SVGGenerator.h"
#include <memory>
#include "ElementWriter.h"
#include "core/utils/Log.h"
#include "svg/SVGContext.h"
#include "svg/xml/XMLWriter.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Size.h"

namespace tgfx {

SVGGenerator::~SVGGenerator() {
  delete _canvas;
  delete _context;
};

Canvas* SVGGenerator::beginGenerate(Context* GPUContext, const ISize& size) {
  ASSERT(GPUContext)
  if (_canvas == nullptr) {
    auto writer = std::make_unique<XMLStreamWriter>(_svgStream, 0);
    _context = new SVGContext(GPUContext, size, std::move(writer), 0);
    _canvas = new Canvas(_context);
    _context->setCanvas(_canvas);
  }

  if (_actively) {
    _canvas->resetMCState();
  } else {
    _actively = true;
  }
  return getCanvas();
}

Canvas* SVGGenerator::getCanvas() const {
  return _actively ? _canvas : nullptr;
};

std::string SVGGenerator::finishGenerate() {
  if (!_actively || _context == nullptr) {
    return "";
  }
  _actively = false;
  _canvas->resetMCState();
  auto svgStr = _svgStream.str();
  delete _canvas;
  _canvas = nullptr;
  delete _context;
  _context = nullptr;
  return _svgStream.str();
};

}  // namespace tgfx