/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Shader.h"

namespace tgfx {

class PDFGradientShader {
 public:
  struct Key {
    GradientType fType;
    GradientInfo fInfo;
    const std::vector<Color>* fColors;
    const std::vector<float>* fStops;
    Matrix fCanvasTransform;
    Matrix fShaderTransform;
    Rect fBBox;
    uint32_t fHash;
  };

  static PDFIndirectReference Make(PDFDocument* doc, Shader* shader, const Matrix& matrix,
                                   const Rect& surfaceBBox);
};

}  // namespace tgfx