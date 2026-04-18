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
#include "ColorSpaceXformHelper.h"
namespace tgfx {

void ColorSpaceXformHelper::setData(UniformData* uniformData,
                                    const ColorSpaceXformSteps* colorSpaceXform) {
  if (colorSpaceXform) {
    flags = colorSpaceXform->flags;
    if (this->applySrcTF()) {
      float srcTF0[4] = {
          colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
          colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
      float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                         colorSpaceXform->srcTransferFunction.e,
                         colorSpaceXform->srcTransferFunction.f, 0.0f};
      uniformData->setData("SrcTF0", srcTF0);
      uniformData->setData("SrcTF1", srcTF1);
    }
    if (this->applySrcOOTF()) {
      uniformData->setData("SrcOOTF", colorSpaceXform->srcOOTF);
    }
    if (this->applyGamutXform()) {
      uniformData->setData("ColorXform", colorSpaceXform->srcToDstMatrix);
    }
    if (this->applyDstOOTF()) {
      uniformData->setData("DstOOTF", colorSpaceXform->dstOOTF);
    }
    if (this->applyDstTF()) {
      float dstTF0[4] = {colorSpaceXform->dstTransferFunctionInverse.g,
                         colorSpaceXform->dstTransferFunctionInverse.a,
                         colorSpaceXform->dstTransferFunctionInverse.b,
                         colorSpaceXform->dstTransferFunctionInverse.c};
      float dstTF1[4] = {colorSpaceXform->dstTransferFunctionInverse.d,
                         colorSpaceXform->dstTransferFunctionInverse.e,
                         colorSpaceXform->dstTransferFunctionInverse.f, 0.0f};
      uniformData->setData("DstTF0", dstTF0);
      uniformData->setData("DstTF1", dstTF1);
    }
  }
}
}  // namespace tgfx
