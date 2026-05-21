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
#include "tgfx/core/Brush.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGCustomWriter.h"
#include "tgfx/svg/SVGExporter.h"
#include "tgfx/svg/SVGPathParser.h"

namespace tgfx {

class ResourceStore;
class ElementWriter;
class PictureImage;
class ColorFilter;

class SVGExportContext : public DrawContext {
 public:
  SVGExportContext(Context* context, const Rect& viewBox, std::unique_ptr<XMLWriter> xmlWriter,
                   uint32_t exportFlags, std::shared_ptr<SVGCustomWriter> customWriter,
                   std::shared_ptr<ColorSpace> targetColorSpace,
                   std::shared_ptr<ColorSpace> assignColorSpace);
  ~SVGExportContext() override = default;

  void setCanvas(Canvas* inputCanvas) {
    canvas = inputCanvas;
  }

  void drawFill(const Brush& brush) override;

  void drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip, const Brush& brush,
                const Stroke* stroke) override;

  void drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override;

  void drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                const Brush& brush) override;

  void drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override;

  void drawMesh(std::shared_ptr<Mesh>, const Matrix&, const ClipStack&, const Brush&) override {
    // SVG does not support mesh rendering.
  }

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const Matrix& matrix, const ClipStack& clip, const Brush& brush) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const Matrix& matrix, const ClipStack& clip,
                     const Brush& brush, SrcRectConstraint constraint) override;

  void drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix, const ClipStack& clip,
                    const Brush& brush, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                   const ClipStack& clip) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const Matrix& matrix, const ClipStack& clip, const Brush& brush) override;

  XMLWriter* getWriter() const {
    return xmlWriter.get();
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
  static bool RequiresViewportReset(const Brush& brush);

  void exportPixmap(const Pixmap& pixmap, const Matrix& matrix, const Brush& brush);

  /**
   * Replays a PictureImage as SVG vector elements, wrapped in a single <g> carrying any
   * colorFilter as a <filter> resource, any non-empty blendModeStyle (as a CSS mix-blend-mode),
   * and any alpha < 1. All three empty/identity skip the corresponding attributes; the wrapper
   * is elided when none apply. The colorFilter must be one of the SVG-expressible types (Matrix
   * or Blend with a mappable BlendMode); the caller is responsible for filtering out the rest.
   * The associated <defs>/<filter> is emitted from inside this function, AFTER the active clip
   * group is closed, so it is placed at the same level as the brush <g> that consumes it.
   */
  void exportPictureImageAsVector(const PictureImage* pictureImage, const Matrix& matrix,
                                  const ClipStack& clip,
                                  const std::shared_ptr<ColorFilter>& colorFilter,
                                  const std::string& blendModeStyle, float alpha);

  void exportGlyphRunAsPath(const GlyphRun& glyphRun, const Matrix& matrix, const Brush& brush,
                            const Stroke* stroke);

  void exportGlyphRunAsText(const GlyphRun& glyphRun, const Matrix& matrix, const Brush& brush,
                            const Stroke* stroke);

  void exportGlyphRunAsImage(const GlyphRun& glyphRun, const Matrix& matrix, const ClipStack& clip,
                             const Brush& brush);

  void applyClip(const ClipStack& clip, const Rect& contentBound);

  void applyClipPath(const Path& clipPath);

  std::string defineClipPath(const Path& clipPath);

  static SVGPathParser::PathEncoding PathEncodingType();

  uint32_t exportFlags = {};
  Context* context = nullptr;
  Rect viewBox = {};
  Canvas* canvas = nullptr;
  const std::unique_ptr<XMLWriter> xmlWriter = nullptr;
  const std::unique_ptr<ResourceStore> resourceBucket = nullptr;
  std::unique_ptr<ElementWriter> rootElement = nullptr;
  SVGTextBuilder textBuilder = {};
  Path currentClipPath = {};
  std::unique_ptr<ElementWriter> clipGroupElement = nullptr;
  std::shared_ptr<SVGCustomWriter> customWriter = {};
  std::shared_ptr<ColorSpace> _targetColorSpace = nullptr;
  std::shared_ptr<ColorSpace> _assignColorSpace = nullptr;
};
}  // namespace tgfx
