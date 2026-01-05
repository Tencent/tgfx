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

namespace tgfx {

class PDFBitmap {
 public:
  /**
   * Serialize a image as an Image Xobject. quality > 100 means lossless
   */
  static PDFIndirectReference Serialize(const std::shared_ptr<Image>& image,
                                        PDFDocumentImpl* document, int encodingQuality = 101);

 private:
  static void SerializeImage(const std::shared_ptr<Image>& image, int encodingQuality,
                             PDFDocumentImpl* doc, PDFIndirectReference ref);
};

}  // namespace tgfx
