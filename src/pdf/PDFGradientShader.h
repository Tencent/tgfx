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

#include "core/shaders/GradientShader.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/GradientType.h"

namespace tgfx {

class PDFGradientShader {
 public:
  struct Key {
    GradientType type;
    GradientInfo info;
    const std::vector<Color>* colors;
    const std::vector<float>* stops;
    Matrix canvasTransform;
    Matrix shaderTransform;
    Rect boundBox;
    uint32_t hash;
  };

  static PDFIndirectReference Make(PDFDocumentImpl* doc, const GradientShader* shader,
                                   const Matrix& matrix, const Rect& surfaceBBox);
};

}  // namespace tgfx
