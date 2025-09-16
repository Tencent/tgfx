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

#pragma once

#include "core/DrawContext.h"
#include "svg/SVGTextBuilder.h"
#include "svg/xml/XMLWriter.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Fill.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGExporter.h"
#include "tgfx/svg/SVGPathParser.h"

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

  void drawFill(const Fill& fill) override;

  void drawRect(const Rect& rect, const MCState& state, const Fill& fill) override;

  void drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                 const Stroke* stroke) override;

  void drawPath(const Path& path, const MCState& state, const Fill& fill) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill,
                 const Stroke* stroke) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Fill& fill) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const MCState& state, const Fill& fill,
                     SrcRectConstraint constraint) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                        const Fill& fill, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Fill& fill) override;

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
  static bool RequiresViewportReset(const Fill& fill);

  void exportPixmap(const Pixmap& pixmap, const MCState& state, const Fill& fill);

  void exportGlyphsAsPath(const std::shared_ptr<GlyphRunList>& glyphRunList, const MCState& state,
                          const Fill& fill, const Stroke* stroke);

  void exportGlyphsAsText(const std::shared_ptr<GlyphRunList>& glyphRunList, const MCState& state,
                          const Fill& fill, const Stroke* stroke);

  void exportGlyphsAsImage(const std::shared_ptr<GlyphRunList>& glyphRunList, const MCState& state,
                           const Fill& fill);

  void applyClipPath(const Path& clipPath);

  static SVGPathParser::PathEncoding PathEncodingType();

  uint32_t exportFlags = {};
  Context* context = nullptr;
  Rect viewBox = {};
  Canvas* canvas = nullptr;
  const std::unique_ptr<XMLWriter> writer = nullptr;
  const std::unique_ptr<ResourceStore> resourceBucket = nullptr;
  std::unique_ptr<ElementWriter> rootElement = nullptr;
  SVGTextBuilder textBuilder = {};
  Path currentClipPath = {};
  std::unique_ptr<ElementWriter> clipGroupElement = nullptr;
};
}  // namespace tgfx
