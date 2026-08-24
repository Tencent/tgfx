/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "pdf/PDFTypes.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {

class PDFBitmap {
 public:
  /**
   * Serialize a image as an Image Xobject. quality > 100 means lossless
   */
  static PDFIndirectReference Serialize(const std::shared_ptr<Image>& image,
                                        PDFDocumentImpl* document, int encodingQuality = 101);

  /**
   * Emits the image XObject body for RGBA_8888 / Unpremultiplied pixels that already live on the
   * CPU. Shared by the synchronous path and by the deferred raster flush so both emit identical
   * objects.
   */
  static void WritePixmap(const Pixmap& pixmap, bool isOpaque, int encodingQuality,
                          PDFDocumentImpl* document, PDFIndirectReference ref);

  /**
   * Converts freshly locked readback pixels into the layout WritePixmap() expects and emits them.
   * srcInfo describes the readback buffer, whose row stride may exceed the width because of the
   * backend's buffer copy alignment. flipY is set when the render target origin is bottom-left.
   */
  static void WriteReadbackPixels(const ImageInfo& srcInfo, const void* srcPixels, bool flipY,
                                  int encodingQuality, PDFDocumentImpl* document,
                                  PDFIndirectReference ref);

  /**
   * Emits a fully transparent 1x1 image XObject for a failed rasterization. The object must be
   * emitted rather than skipped: an object number that is never emitted keeps offset 0 in the cross
   * reference table and corrupts the whole file.
   */
  static void WritePlaceholder(PDFDocumentImpl* document, PDFIndirectReference ref);

 private:
  static void SerializeImage(const std::shared_ptr<Image>& image, int encodingQuality,
                             PDFDocumentImpl* doc, PDFIndirectReference ref);
};

}  // namespace tgfx
