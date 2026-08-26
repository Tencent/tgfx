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

#include "GLSLRRectEffect.h"
#include "core/utils/Log.h"

namespace tgfx {

PlacementPtr<RRectEffect> RRectEffect::Make(BlockAllocator* allocator, const RRect& rRect,
                                            const Matrix& matrix, bool antiAlias) {
  DEBUG_ASSERT(rRect.type() != RRect::Type::Rect);
  // The analytic AA relies on a linear device-to-local inverse map, which cannot represent a
  // perspective matrix.
  DEBUG_ASSERT(!matrix.hasPerspective());
  if (matrix.hasPerspective()) {
    return nullptr;
  }

  RRect adjustedRRect = rRect;
  Matrix deviceToLocal = Matrix::I();
  if (!matrix.isIdentity()) {
    const auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) {
      return nullptr;
    }
    // Bake axisScales into the rrect so the shader's local-space distance comparison
    // matches device-space half-pixel AA semantics.
    adjustedRRect.scale(scales.x, scales.y);
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
    if (!adjustedMatrix.invert(&deviceToLocal)) {
      return nullptr;
    }
  }
  return allocator->make<GLSLRRectEffect>(adjustedRRect.rect(), adjustedRRect.radii(),
                                          deviceToLocal, antiAlias);
}

GLSLRRectEffect::GLSLRRectEffect(const Rect& localRect, const std::array<Point, 4>& radii,
                                 const Matrix& deviceToLocal, bool antiAlias)
    : RRectEffect(localRect, radii, deviceToLocal, antiAlias) {
}

void GLSLRRectEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  const auto rectName =
      uniformHandler->addUniform("LocalRect", UniformFormat::Float4, ShaderStage::Fragment);
  const auto radiiXName =
      uniformHandler->addUniform("RadiiX", UniformFormat::Float4, ShaderStage::Fragment);
  const auto radiiYName =
      uniformHandler->addUniform("RadiiY", UniformFormat::Float4, ShaderStage::Fragment);
  const auto antiAliasName =
      uniformHandler->addUniform("AntiAlias", UniformFormat::Float, ShaderStage::Fragment);

  // Step 1: get local-space coordinate.
  if (needTransform()) {
    const auto deviceToLocalName =
        uniformHandler->addUniform("DeviceToLocal", UniformFormat::Float3x3, ShaderStage::Fragment);
    fragBuilder->codeAppendf("highp vec3 hl = %s * vec3(gl_FragCoord.xy, 1.0);",
                             deviceToLocalName.c_str());
    fragBuilder->codeAppend("highp vec2 local = hl.xy / hl.z;");
  } else {
    fragBuilder->codeAppend("highp vec2 local = gl_FragCoord.xy;");
  }

  // Step 2: arc-box dispatch. Each corner has a quadrant-local arc box of size radii[i].
  // Order matches the C++ side: TL, TR, BR, BL.
  fragBuilder->codeAppendf("highp vec4 cornersX = vec4(%s.x, %s.z, %s.z, %s.x);", rectName.c_str(),
                           rectName.c_str(), rectName.c_str(), rectName.c_str());
  fragBuilder->codeAppendf("highp vec4 cornersY = vec4(%s.y, %s.y, %s.w, %s.w);", rectName.c_str(),
                           rectName.c_str(), rectName.c_str(), rectName.c_str());
  // signsX/Y flip the local coordinate so that all four arc-box queries become first-quadrant.
  fragBuilder->codeAppend("vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);");
  fragBuilder->codeAppend("vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);");
  fragBuilder->codeAppend("highp vec4 dx = (vec4(local.x) - cornersX) * signsX;");
  fragBuilder->codeAppend("highp vec4 dy = (vec4(local.y) - cornersY) * signsY;");
  // inBox is 1.0 for each corner whose arc box contains the fragment.
  fragBuilder->codeAppendf("vec4 inBox = step(dx, %s) * step(dy, %s);", radiiXName.c_str(),
                           radiiYName.c_str());
  // One-hot collapse: when adjacent arc boxes share a fragment along their boundary, keep
  // the first hit so subsequent queries (arcCenter, radii) collapse to a single corner.
  fragBuilder->codeAppend(
      "vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, "
      "inBox.x + inBox.y + inBox.z);");
  fragBuilder->codeAppend("inBox *= step(prefixSum, vec4(0.5));");
  fragBuilder->codeAppend("float inAnyCorner = dot(inBox, vec4(1.0));");

  // Step 3: select arc center / radii via dot products with the one-hot mask.
  fragBuilder->codeAppendf("highp vec4 arcCentersX = cornersX + signsX * %s;", radiiXName.c_str());
  fragBuilder->codeAppendf("highp vec4 arcCentersY = cornersY + signsY * %s;", radiiYName.c_str());
  fragBuilder->codeAppend(
      "highp vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));");
  fragBuilder->codeAppendf("highp vec2 r = vec2(dot(inBox, %s), dot(inBox, %s));",
                           radiiXName.c_str(), radiiYName.c_str());

  // Step 4: half-pixel radius guard. safeRadii is used as a divisor below, so a zero or near-zero
  // radius would divide by zero or produce unstable values. Clamping to 0.5 avoids that, and since
  // 0.5 is sub-pixel the corner still looks the same.
  fragBuilder->codeAppend("highp vec2 safeRadii = max(r, vec2(0.5));");

  // Step 5: ellipse SDF (Sampson distance) inside the corner region.
  fragBuilder->codeAppend("highp vec2 offset = (local - arcCenter) / safeRadii;");
  fragBuilder->codeAppend("highp vec2 safeOffset = max(abs(offset), vec2(1.0/4096.0));");
  fragBuilder->codeAppend("highp float test = dot(safeOffset, safeOffset) - 1.0;");
  fragBuilder->codeAppend("highp vec2 grad = 2.0 * safeOffset / safeRadii;");
  fragBuilder->codeAppend("highp float gradDot = max(dot(grad, grad), 1.1755e-38);");
  fragBuilder->codeAppend("highp float cornerDist = test * inversesqrt(gradDot);");

  // Step 6: edge / center distance (negative inside, positive outside).
  fragBuilder->codeAppendf("highp vec2 d0 = local - %s.xy;", rectName.c_str());
  fragBuilder->codeAppendf("highp vec2 d1 = %s.zw - local;", rectName.c_str());
  fragBuilder->codeAppend("highp float edgeDist = -min(min(d0.x, d0.y), min(d1.x, d1.y));");

  // Step 7: choose corner SDF or edge SDF based on the arc-box mask.
  fragBuilder->codeAppend("highp float dist = mix(edgeDist, cornerDist, inAnyCorner);");

  // Step 8: AA / NonAA coverage via uniform mix.
  fragBuilder->codeAppend("highp float aaCov = clamp(0.5 - dist, 0.0, 1.0);");
  fragBuilder->codeAppend("highp float nonAACov = step(dist, 0.0);");
  fragBuilder->codeAppendf("highp float coverage = mix(nonAACov, aaCov, %s);",
                           antiAliasName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(),
                           args.inputColor.c_str());
}

void GLSLRRectEffect::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("LocalRect", localRect);
  // Pack the per-corner radii into two vec4 uniforms (RadiiX, RadiiY) in [TL, TR, BR, BL]
  // order to match the shader.
  const std::array<float, 4> radiiXValues = {radii[0].x, radii[1].x, radii[2].x, radii[3].x};
  const std::array<float, 4> radiiYValues = {radii[0].y, radii[1].y, radii[2].y, radii[3].y};
  fragmentUniformData->setData("RadiiX", radiiXValues);
  fragmentUniformData->setData("RadiiY", radiiYValues);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData("AntiAlias", antiAliasValue);
  if (needTransform()) {
    fragmentUniformData->setData("DeviceToLocal", deviceToLocal());
  }
}

}  // namespace tgfx
