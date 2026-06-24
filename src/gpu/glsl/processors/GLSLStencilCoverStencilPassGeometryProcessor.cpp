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

#include "GLSLStencilCoverStencilPassGeometryProcessor.h"

namespace tgfx {
PlacementPtr<StencilCoverStencilPassGeometryProcessor>
StencilCoverStencilPassGeometryProcessor::Make(BlockAllocator* allocator,
                                               const Matrix& viewMatrix) {
  return allocator->make<GLSLStencilCoverStencilPassGeometryProcessor>(viewMatrix);
}

GLSLStencilCoverStencilPassGeometryProcessor::GLSLStencilCoverStencilPassGeometryProcessor(
    const Matrix& viewMatrix)
    : StencilCoverStencilPassGeometryProcessor(viewMatrix) {
}

void GLSLStencilCoverStencilPassGeometryProcessor::emitCode(EmitArgs& args) const {
  // Stencil pass shader, the GPU half of the Loop-Blinn quadratic fill (Loop & Blinn,
  // SIGGRAPH 2005 — see StencilCoverPathTessellator.h for the full reference). Ported from
  // demo/fillRenderer.js Pass 1:
  //   - Vertex: forward the position through the view matrix and emit normalized device
  //     coords via the project's standard helper (which also handles the Y-flip for
  //     BottomLeft origin render targets). The per-vertex KLM coefficients written by the
  //     tessellator are forwarded unchanged to the fragment stage as a varying — hardware
  //     linear interpolation across the curve triangle is exactly what makes the Loop-Blinn
  //     mapping work (the locus `k*k = l` of the interpolated values traces the bezier).
  //   - Fragment: run the Loop-Blinn implicit-curve test. For a quadratic the discriminant
  //     is `f(k, l) = k*k - l`:
  //         f < 0 → fragment on the concave (interior) side of the bezier — keep
  //         f = 0 → fragment lies on the bezier curve itself          — keep
  //         f > 0 → fragment on the convex (exterior) side             — discard
  //     Pixels that survive the test go on to update the stencil buffer per the configured
  //     stencil op. The third KLM channel (klm.z, paper's `m`) is reserved for the cubic
  //     extension and unused here.
  //   - Colour output is a placeholder. The bound pipeline disables colour writes via
  //     ColorWriteMask = 0, so the values written here are dropped before reaching the
  //     colour attachment.
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->codeAppendf("highp vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                           matrixName.c_str(), position.name().c_str());

  auto klmVarying = varyingHandler->addVarying("KLM", SLType::Float3);
  vertBuilder->codeAppendf("%s = %s;", klmVarying.vsOut().c_str(), klm.name().c_str());

  // Loop-Blinn quadratic implicit-curve test: discard fragments where k*k - l > 0
  // (convex / exterior side of the bezier).
  fragBuilder->codeAppendf("if (%s.x * %s.x - %s.y > 0.0) { discard; }", klmVarying.fsIn().c_str(),
                           klmVarying.fsIn().c_str(), klmVarying.fsIn().c_str());
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());

  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLStencilCoverStencilPassGeometryProcessor::setData(UniformData* vertexUniformData,
                                                           UniformData*,
                                                           FPCoordTransformIter*) const {
  vertexUniformData->setData("Matrix", viewMatrix);
}
}  // namespace tgfx
