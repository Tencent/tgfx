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

#include "GLSLBezierStencilGeometryProcessor.h"

namespace tgfx {
PlacementPtr<BezierStencilGeometryProcessor> BezierStencilGeometryProcessor::Make(
    BlockAllocator* allocator, const Matrix& viewMatrix) {
  return allocator->make<GLSLBezierStencilGeometryProcessor>(viewMatrix);
}

GLSLBezierStencilGeometryProcessor::GLSLBezierStencilGeometryProcessor(const Matrix& viewMatrix)
    : BezierStencilGeometryProcessor(viewMatrix) {
}

void GLSLBezierStencilGeometryProcessor::emitCode(EmitArgs& args) const {
  // Stencil pass shader, ported from demo/fillRenderer.js Pass 1:
  //   - Vertex: forward the position through the view matrix and emit normalized device coords
  //     via the project's standard helper (which also handles the Y-flip for BottomLeft origin
  //     render targets).
  //   - Fragment: discard pixels outside the implicit bezier curve. The classic Loop-Blinn
  //     test for quadratic curves is `klm.x * klm.x - klm.y > 0` — pixels failing the test sit
  //     outside the filled region. The third KLM channel is reserved for the future cubic-
  //     curve extension and unused here.
  //   - Colour output is a placeholder. The bound pipeline disables colour writes via
  //     ColorWriteMask = 0, so the values written here are dropped before reaching the colour
  //     attachment.
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

  // demo Pass 1 fragment: if (uv.x*uv.x - uv.y > 0.0) discard;
  fragBuilder->codeAppendf("if (%s.x * %s.x - %s.y > 0.0) { discard; }", klmVarying.fsIn().c_str(),
                           klmVarying.fsIn().c_str(), klmVarying.fsIn().c_str());
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());

  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLBezierStencilGeometryProcessor::setData(UniformData* vertexUniformData, UniformData*,
                                                 FPCoordTransformIter*) const {
  vertexUniformData->setData("Matrix", viewMatrix);
}
}  // namespace tgfx
