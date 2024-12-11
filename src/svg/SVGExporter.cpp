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

#include "tgfx/svg/SVGExporter.h"
#include <cstdint>
#include <memory>
#include "ElementWriter.h"
#include "core/utils/Log.h"
#include "svg/SVGExportingContext.h"
#include "svg/xml/XMLWriter.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Size.h"

namespace tgfx {

SVGExporter::~SVGExporter() {
  delete canvas;
  delete context;
};

Canvas* SVGExporter::beginExporting(Context* gpuContext, const ISize& size,
                                    uint32_t exportingOptions) {
  ASSERT(gpuContext)
  if (canvas == nullptr) {
    uint32_t xmlOptions = exportingOptions & NoPrettyXML ? XMLStreamWriter::NoPretty : 0;
    auto writer = std::make_unique<XMLStreamWriter>(svgStream, xmlOptions);
    context = new SVGExportingContext(gpuContext, size, std::move(writer), exportingOptions);
    canvas = new Canvas(context);
    context->setCanvas(canvas);
  }

  if (actively) {
    canvas->resetMCState();
  } else {
    actively = true;
  }
  return getCanvas();
}

Canvas* SVGExporter::getCanvas() const {
  return actively ? canvas : nullptr;
};

std::string SVGExporter::finishExportingAsString() {
  if (!actively || context == nullptr) {
    return "";
  }
  actively = false;
  canvas->resetMCState();
  delete canvas;
  canvas = nullptr;
  delete context;
  context = nullptr;

  auto svgString = svgStream.str();
  svgStream.str("");
  svgStream.clear();
  return svgString;
};

}  // namespace tgfx