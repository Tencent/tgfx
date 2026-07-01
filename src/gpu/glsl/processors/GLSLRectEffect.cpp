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

namespace tgfx {

PlacementPtr<RectEffect> RectEffect::Make(BlockAllocator* allocator, const Rect& localRect,
                                          const Matrix& matrix, bool antiAlias) {
  if (matrix.hasPerspective()) {
    return nullptr;
  }
  Rect adjustedRect = localRect;
  Matrix deviceToLocal = Matrix::I();
  bool needTransform = false;
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
    needTransform = true;
  }
  return allocator->make<GLSLRectEffect>(adjustedRect, deviceToLocal, needTransform, antiAlias);
}

void RectEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(needTransform ? 1 : 0));
}

GLSLRectEffect::GLSLRectEffect(const Rect& localRect, const Matrix& deviceToLocal,
                               bool needTransform, bool antiAlias)
    : RectEffect(localRect, deviceToLocal, needTransform, antiAlias) {
}

void GLSLRectEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  const auto rectName =
      uniformHandler->addUniform("LocalRect", UniformFormat::Float4, ShaderStage::Fragment);
  const auto antiAliasName =
      uniformHandler->addUniform("AntiAlias", UniformFormat::Float, ShaderStage::Fragment);

  std::string deviceToLocalName;
  if (needTransform) {
    deviceToLocalName =
        uniformHandler->addUniform("DeviceToLocal", UniformFormat::Float3x3, ShaderStage::Fragment);
  }

  // Step 1: get local-space coordinates.
  if (needTransform) {
    fragBuilder->codeAppendf("vec3 hl = %s * vec3(gl_FragCoord.xy, 1.0);",
                             deviceToLocalName.c_str());
    fragBuilder->codeAppend("vec2 local = hl.xy / hl.z;");
  } else {
    fragBuilder->codeAppend("vec2 local = gl_FragCoord.xy;");
  }

  // Step 2: separable 1D coverage product. Half-pixel AA band absorbed into clamp limits so
  // LocalRect uniform always carries the original (un-outset) rectangle.
  // aaCovXY equals 0.5 on the rectangle edge, 1.0 inside, 0.0 outside.
  fragBuilder->codeAppendf("vec4 dAA = clamp(vec4(local - %s.xy, %s.zw - local), -0.5, 0.5);",
                           rectName.c_str(), rectName.c_str());
  fragBuilder->codeAppend("vec2 aaCovXY = dAA.xy + dAA.zw;");
  // box-filter coverage: at the rectangle's vertex aaCov = 0.25 (1/4 of the pixel inside).
  // This matches the legacy AARectEffect behavior and is the standard separable AA fill result.
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

void GLSLRectEffect::onSetData(UniformData* /*vertexUniformData*/,
                               UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("LocalRect", localRect);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData("AntiAlias", antiAliasValue);
  if (needTransform) {
    fragmentUniformData->setData("DeviceToLocal", deviceToLocal);
  }
}

}  // namespace tgfx
