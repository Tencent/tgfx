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

#include <_types/_uint32_t.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include "core/DrawContext.h"
#include "core/FillStyle.h"
#include "svg/SVGTextBuilder.h"
#include "svg/SVGUtils.h"
#include "svg/xml/XMLWriter.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGExporter.h"

namespace tgfx {

class ResourceStore;
class ElementWriter;

class SVGExportContext : public DrawContext {
 public:
  SVGExportContext(Context* context, const Rect& viewBox, std::unique_ptr<XMLWriter> writer,
                   uint32_t exportFlags);
  ~SVGExportContext() override = default;

  void setCanvas(Canvas* inputCanvas) {
    canvas = inputCanvas;
  }

  void drawStyle(const MCState& state, const FillStyle& style) override;

  void drawRect(const Rect& rect, const MCState& state, const FillStyle& style) override;

  void drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                 const FillStyle& style) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const FillStyle& style) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state,
                     const FillStyle& style) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const Stroke* stroke,
                        const MCState& state, const FillStyle& style) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const FillStyle& style) override;

  XMLWriter* getWriter() const {
    return writer.get();
  }

  /**
   * Draws a image onto a surface and reads the pixels from the surface.
   */
  static Bitmap ImageExportToBitmap(Context* context, const std::shared_ptr<Image>& image);

  /**
   * Returns the encoded pixel data if the image was created from a supported encoded format.
   */
  static std::shared_ptr<Data> ImageToEncodedData(const std::shared_ptr<Image>& image);

 private:
  /**
   * Determine if the paint requires us to reset the viewport.Currently, we do this whenever the
   * paint shader calls for a repeating image.
   */
  static bool RequiresViewportReset(const FillStyle& fill);

  void exportPixmap(const Pixmap& pixmap, const MCState& state, const FillStyle& style);

  void exportGlyphsAsPath(const std::shared_ptr<GlyphRunList>& glyphRunList, const MCState& state,
                          const FillStyle& style, const Stroke* stroke);

  void exportGlyphsAsText(const std::shared_ptr<GlyphRunList>& glyphRunList, const MCState& state,
                          const FillStyle& style, const Stroke* stroke);

  void exportGlyphsAsImage(const std::shared_ptr<GlyphRunList>& glyphRunList, const MCState& state,
                           const FillStyle& style);

  void applyClipPath(const Path& clipPath);

  static PathEncoding PathEncoding();

  uint32_t exportFlags = {};
  Context* context = nullptr;
  Rect viewBox = Rect::MakeEmpty();
  Canvas* canvas = nullptr;
  const std::unique_ptr<XMLWriter> writer = nullptr;
  const std::unique_ptr<ResourceStore> resourceBucket = nullptr;
  std::unique_ptr<ElementWriter> rootElement = nullptr;
  SVGTextBuilder textBuilder = {};
  Path currentClipPath = {};
  std::unique_ptr<ElementWriter> clipGroupElement = nullptr;
};
}  // namespace tgfx