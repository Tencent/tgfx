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
#include <string>
#include <utility>
#include "ElementWriter.h"
#include "core/utils/Log.h"
#include "svg/SVGExportContext.h"
#include "svg/SVGUtils.h"
#include "svg/xml/XMLWriter.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Size.h"

namespace tgfx {

std::shared_ptr<SVGExporter> SVGExporter::Make(const std::shared_ptr<WriteStream>& svgStream,
                                               Context* context, const Rect& viewBox,
                                               uint32_t exportFlags,
                                               const std::shared_ptr<SVGCustomWriter>& customWriter,
                                               std::shared_ptr<ColorSpace> targetColorSpace,
                                               std::shared_ptr<ColorSpace> assignColorSpace) {
  if (!context || !svgStream || viewBox.isEmpty()) {
    return nullptr;
  }
  return std::shared_ptr<SVGExporter>(new SVGExporter(svgStream, context, viewBox, exportFlags,
                                                      customWriter, std::move(targetColorSpace),
                                                      std::move(assignColorSpace)));
}

SVGExporter::SVGExporter(const std::shared_ptr<WriteStream>& svgStream, Context* context,
                         const Rect& viewBox, uint32_t exportFlags,
                         const std::shared_ptr<SVGCustomWriter>& customWriter,
                         std::shared_ptr<ColorSpace> targetColorSpace,
                         std::shared_ptr<ColorSpace> assignColorSpace)
    : context(context), userStream(svgStream), bufferStream(MemoryWriteStream::Make()) {
  // The XML is staged in an internal buffer so pending image tokens can still be substituted before
  // the text reaches the caller's stream.
  auto streamWriter = std::make_unique<XMLStreamWriter>(
      bufferStream, exportFlags & SVGExportFlags::DisablePrettyXML);
  drawContext =
      new SVGExportContext(context, viewBox, std::move(streamWriter), exportFlags, customWriter,
                           std::move(targetColorSpace), std::move(assignColorSpace));
  canvas = new Canvas(drawContext);
  drawContext->setCanvas(canvas);
};

SVGExporter::~SVGExporter() {
  close();
  delete drawContext;
  drawContext = nullptr;
};

Canvas* SVGExporter::getCanvas() const {
  return canvas;
};

void SVGExporter::close() {
  if (closed) {
    return;
  }
  delete canvas;
  canvas = nullptr;
  drawContext->finish();
  closed = true;
  resolveArrivedImages();
  flushToUserStream();
}

bool SVGExporter::isReadyToClose() {
  if (closed || drawContext == nullptr) {
    return true;
  }
  // Encoding here instead of at close() bounds the memory to the readbacks still in flight.
  resolveArrivedImages();
  for (const auto& pending : *drawContext->pendingSink()) {
    if (pending.readback != nullptr) {
      return false;
    }
  }
  return true;
}

void SVGExporter::resolveArrivedImages() {
  if (drawContext == nullptr || context == nullptr) {
    return;
  }
  for (auto& pending : *drawContext->pendingSink()) {
    // Checking readiness first avoids the synchronous GPU wait inside lockPixels().
    if (pending.readback == nullptr || !pending.readback->isReady(context)) {
      continue;
    }
    auto pixels = pending.readback->lockPixels(context, pending.flipY);
    if (pixels != nullptr) {
      Pixmap pixmap(pending.readback->info().makeColorSpace(pending.colorSpace), pixels);
      if (auto encoded = AsDataUri(pixmap)) {
        pending.dataUri = static_cast<const char*>(encoded->data());
      }
    }
    pending.readback->unlockPixels(context);
    // Dropping the readback releases its GPU buffer and marks the image as resolved. A failed
    // encoding keeps an empty dataUri because retrying it would fail the same way.
    pending.readback = nullptr;
  }
}

void SVGExporter::flushToUserStream() {
  if (flushed || bufferStream == nullptr || userStream == nullptr) {
    return;
  }
  flushed = true;
  auto* pendings = drawContext == nullptr ? nullptr : drawContext->pendingSink();
  if (pendings == nullptr || pendings->empty()) {
    auto data = bufferStream->readData();
    if (data != nullptr) {
      userStream->write(data->bytes(), data->size());
      userStream->flush();
    }
    return;
  }
  // Substituting in a plain string and writing it straight to the user stream avoids copying the
  // whole document, base64 payloads included, two more times.
  auto svgText = bufferStream->readString();
  bufferStream->reset();
  for (const auto& pending : *pendings) {
    if (pending.dataUri.empty()) {
      LOGE(
          "SVGExporter::flushToUserStream() Pixels never arrived for an image, dropping its href!");
    }
    size_t position = 0;
    while ((position = svgText.find(pending.token, position)) != std::string::npos) {
      // An image without a usable href renders as nothing, unlike a leftover token that would make
      // the whole SVG unloadable.
      svgText.replace(position, pending.token.size(), pending.dataUri);
      position += pending.dataUri.empty() ? 1 : pending.dataUri.size();
    }
  }
  pendings->clear();
  userStream->write(svgText.data(), svgText.size());
  userStream->flush();
}

}  // namespace tgfx
