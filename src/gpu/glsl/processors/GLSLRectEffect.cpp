/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "GLSLRectEffect.h"
#include "core/utils/Log.h"

namespace tgfx {

PlacementPtr<RectEffect> RectEffect::Make(BlockAllocator* allocator, const Rect& localRect,
                                          const Matrix& matrix, bool antiAlias) {
  // The analytic AA relies on a linear device-to-local inverse map, which cannot represent a
  // perspective matrix.
  DEBUG_ASSERT(!matrix.hasPerspective());
  if (matrix.hasPerspective()) {
    return nullptr;
  }

  Rect adjustedRect = localRect;
  Matrix deviceToLocal = Matrix::I();
  if (!matrix.isIdentity()) {
    // Decompose matrix as M = M_axisFree * diag(axisScales). Bake axisScales into the local
    // rect so the deviceToLocal matrix used at runtime preserves the device-space half-pixel
    // AA band semantics.
    const auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) {
      return nullptr;
    }
    adjustedRect.scale(scales.x, scales.y);
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
    if (!adjustedMatrix.invert(&deviceToLocal)) {
      return nullptr;
    }
  }
  return allocator->make<GLSLRectEffect>(adjustedRect, deviceToLocal, antiAlias);
}

GLSLRectEffect::GLSLRectEffect(const Rect& localRect, const Matrix& deviceToLocal, bool antiAlias)
    : RectEffect(localRect, deviceToLocal, antiAlias) {
}

void GLSLRectEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  const auto rectName =
      uniformHandler->addUniform("LocalRect", UniformFormat::Float4, ShaderStage::Fragment);
  const auto antiAliasName =
      uniformHandler->addUniform("AntiAlias", UniformFormat::Float, ShaderStage::Fragment);

  // Step 1: get local-space coordinates.
  if (needTransform()) {
    const auto deviceToLocalName =
        uniformHandler->addUniform("DeviceToLocal", UniformFormat::Float3x3, ShaderStage::Fragment);
    fragBuilder->codeAppendf("vec3 hl = %s * vec3(gl_FragCoord.xy, 1.0);",
                             deviceToLocalName.c_str());
    fragBuilder->codeAppend("vec2 local = hl.xy / hl.z;");
  } else {
    fragBuilder->codeAppend("vec2 local = gl_FragCoord.xy;");
  }

  // Step 2: separable 1D coverage product. Clamp the signed distance to each edge into a half-pixel
  // band, giving each axis a coverage of 0.5 on the edge, 1.0 inside, and 0.0 outside.
  fragBuilder->codeAppendf("vec4 dAA = clamp(vec4(local - %s.xy, %s.zw - local), -0.5, 0.5);",
                           rectName.c_str(), rectName.c_str());
  fragBuilder->codeAppend("vec2 aaCovXY = dAA.xy + dAA.zw;");
  // box-filter coverage: the product of the two axis coverages, giving 0.25 at the rectangle's
  // vertex where a quarter of the pixel is inside.
  fragBuilder->codeAppend("float aaCov = aaCovXY.x * aaCovXY.y;");
  // NonAA coverage uses hard step at the original edge.
  fragBuilder->codeAppendf(
      "float nonAACov = step(%s.x, local.x) * step(local.x, %s.z) * "
      "step(%s.y, local.y) * step(local.y, %s.w);",
      rectName.c_str(), rectName.c_str(), rectName.c_str(), rectName.c_str());
  fragBuilder->codeAppendf("float coverage = mix(nonAACov, aaCov, %s);", antiAliasName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(),
                           args.inputColor.c_str());
}

void GLSLRectEffect::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("LocalRect", localRect);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData("AntiAlias", antiAliasValue);
  if (needTransform()) {
    fragmentUniformData->setData("DeviceToLocal", deviceToLocal());
  }
}

}  // namespace tgfx
