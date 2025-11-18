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

#include "tgfx/svg/SVGExporter.h"
#include <cstdint>
#include <memory>
#include <utility>
#include "ElementWriter.h"
#include "../../include/tgfx/core/Log.h"
#include "svg/SVGExportContext.h"
#include "svg/xml/XMLWriter.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Size.h"

namespace tgfx {

std::shared_ptr<SVGExporter> SVGExporter::Make(const std::shared_ptr<WriteStream>& svgStream,
                                               Context* context, const Rect& viewBox,
                                               uint32_t exportFlags) {
  if (!context || !svgStream || viewBox.isEmpty()) {
    return nullptr;
  }
  return std::shared_ptr<SVGExporter>(new SVGExporter(svgStream, context, viewBox, exportFlags));
}

SVGExporter::SVGExporter(const std::shared_ptr<WriteStream>& svgStream, Context* context,
                         const Rect& viewBox, uint32_t exportFlags) {
  auto writer =
      std::make_unique<XMLStreamWriter>(svgStream, exportFlags & SVGExportFlags::DisablePrettyXML);
  drawContext = new SVGExportContext(context, viewBox, std::move(writer), exportFlags);
  canvas = new Canvas(drawContext);
  drawContext->setCanvas(canvas);
};

SVGExporter::~SVGExporter() {
  delete canvas;
  delete drawContext;
};

Canvas* SVGExporter::getCanvas() const {
  return canvas;
};

void SVGExporter::close() {
  delete canvas;
  canvas = nullptr;
  delete drawContext;
  drawContext = nullptr;
};

}  // namespace tgfx