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
   * Emits the image XObject body for pixels that already live on the CPU. Shared by the synchronous
   * path and by PDFDocumentImpl::flushPendingRasters() so both emit identical objects.
   * @param pixmap RGBA_8888 / Unpremultiplied pixels, as produced by both readback paths.
   */
  static void WritePixmap(const Pixmap& pixmap, bool isOpaque, int encodingQuality,
                          PDFDocumentImpl* document, PDFIndirectReference ref);

  /**
   * Converts freshly locked readback pixels into the layout WritePixmap() expects and emits them.
   * @param srcInfo Describes the readback buffer, whose row stride may be larger than the width
   * because of the backend's buffer copy alignment.
   * @param flipY Set when the render target origin is bottom-left.
   */
  static void WriteReadbackPixels(const ImageInfo& srcInfo, const void* srcPixels, bool flipY,
                                  int encodingQuality, PDFDocumentImpl* document,
                                  PDFIndirectReference ref);

  /**
   * Emits a blank 1x1 image XObject. Every reserved object number must be emitted: an object that
   * is never written keeps offset 0 in the cross reference table, which corrupts the whole file. So
   * a failed rasterization writes this rather than skipping the object.
   */
  static void WritePlaceholder(PDFDocumentImpl* document, PDFIndirectReference ref);

 private:
  static void SerializeImage(const std::shared_ptr<Image>& image, int encodingQuality,
                             PDFDocumentImpl* doc, PDFIndirectReference ref);
};

}  // namespace tgfx
