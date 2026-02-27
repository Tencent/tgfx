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
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

class PDFShader {
 public:
  static PDFIndirectReference Make(PDFDocumentImpl* doc, const std::shared_ptr<Shader>& shader,
                                   const Matrix& canvasTransform, const Rect& surfaceBBox,
                                   Color paintColor);

 private:
  static PDFIndirectReference MakeImageShader(PDFDocumentImpl* doc, Matrix finalMatrix,
                                              TileMode tileModesX, TileMode tileModesY, Rect bBox,
                                              const std::shared_ptr<Image>& image,
                                              Color paintColor);

  static PDFIndirectReference MakeFallbackShader(PDFDocumentImpl* doc,
                                                 const std::shared_ptr<Shader>& shader,
                                                 const Matrix& canvasTransform,
                                                 const Rect& surfaceBBox, Color paintColor);
};
}  // namespace tgfx
