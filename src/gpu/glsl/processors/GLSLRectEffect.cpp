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
#include "gpu/AOTEffect.h"
#include "gpu/shaders/KernelContract.h"

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

  const auto rectName = uniformHandler->addUniform(ClipContract::LocalRect, UniformFormat::Float4,
                                                   ShaderStage::Fragment);
  const auto antiAliasName = uniformHandler->addUniform(
      ClipContract::AntiAlias, UniformFormat::Float, ShaderStage::Fragment);

  // Step 1: get local-space coordinates.
  if (needTransform()) {
    const auto deviceToLocalName = uniformHandler->addUniform(
        ClipContract::DeviceToLocal, UniformFormat::Float3x3, ShaderStage::Fragment);
    fragBuilder->codeAppendf("highp vec3 hl = %s * vec3(gl_FragCoord.xy, 1.0);",
                             deviceToLocalName.c_str());
    fragBuilder->codeAppend("highp vec2 local = hl.xy / hl.z;");
  } else {
    fragBuilder->codeAppend("highp vec2 local = gl_FragCoord.xy;");
  }

  // Step 2: separable 1D coverage product. Clamp the signed distance to each edge into a half-pixel
  // band, giving each axis a coverage of 0.5 on the edge, 1.0 inside, and 0.0 outside.
  fragBuilder->codeAppendf("highp vec4 dAA = clamp(vec4(local - %s.xy, %s.zw - local), -0.5, 0.5);",
                           rectName.c_str(), rectName.c_str());
  fragBuilder->codeAppend("highp vec2 aaCovXY = dAA.xy + dAA.zw;");
  // box-filter coverage: the product of the two axis coverages, giving 0.25 at the rectangle's
  // vertex where a quarter of the pixel is inside.
  fragBuilder->codeAppend("highp float aaCov = aaCovXY.x * aaCovXY.y;");
  // NonAA coverage uses hard step at the original edge.
  fragBuilder->codeAppendf(
      "highp float nonAACov = step(%s.x, local.x) * step(local.x, %s.z) * "
      "step(%s.y, local.y) * step(local.y, %s.w);",
      rectName.c_str(), rectName.c_str(), rectName.c_str(), rectName.c_str());
  fragBuilder->codeAppendf("highp float coverage = mix(nonAACov, aaCov, %s);",
                           antiAliasName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(),
                           args.inputColor.c_str());
}

void GLSLRectEffect::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  fragmentUniformData->setData(ClipContract::LocalRect, localRect);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData(ClipContract::AntiAlias, antiAliasValue);
  if (needTransform()) {
    fragmentUniformData->setData(ClipContract::DeviceToLocal, deviceToLocal());
  }
  if (fragmentUniformData->hasField(ClipContract::Rect) && isDeviceSpaceRect()) {
    // The precompiled level1 kernels evaluate a direct analytic clip through the shared
    // Rect/HasClip contract (clip_coverage.inc), which expects the device-space rect
    // uploaded half a pixel outward so the coverage ramp centers on the geometric boundary.
    fragmentUniformData->setData(ClipContract::Rect, localRect.makeOutset(0.5f, 0.5f));
  }
  if (fragmentUniformData->hasField(ClipContract::HasClip)) {
    // HasClip selects the contract branch: 1 = device-space AA rect (Rect with the half-pixel
    // outset above), 3 = local-space AA rect (LocalRect/DeviceToLocal, written by the JIT-path
    // uploads above with the exact local rect and the device-to-local matrix).
    int hasClip = isDeviceSpaceRect() ? 1 : 3;
    fragmentUniformData->setData(ClipContract::HasClip, hasClip);
  }
}

bool RectEffect::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input, AOTNodeID* output) const {
  // The AOT rect-coverage node evaluates the analytic AA in either space through the
  // deviceToLocal matrix (identity for the device-space form); the NonAA hard edge stays on the
  // runtime route.
  if (builder == nullptr || output == nullptr || !antiAlias) {
    return false;
  }
  AOTRectCoverageParameters parameters = {};
  parameters.rect = {localRect.left, localRect.top, localRect.right, localRect.bottom};
  const auto& matrix = deviceToLocal();
  parameters.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                              matrix[5], matrix[6], matrix[7], matrix[8]};
  return builder->addRectCoverage(input, parameters, output);
}

}  // namespace tgfx
