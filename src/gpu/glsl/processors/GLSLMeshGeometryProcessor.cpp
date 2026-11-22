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

#include "GLSLMeshGeometryProcessor.h"

namespace tgfx {

PlacementPtr<MeshGeometryProcessor> MeshGeometryProcessor::Make(BlockAllocator* allocator,
                                                                bool hasTexCoords, bool hasColors,
                                                                bool hasCoverage, PMColor color,
                                                                const Matrix& viewMatrix) {
  return allocator->make<GLSLMeshGeometryProcessor>(hasTexCoords, hasColors, hasCoverage, color,
                                                    viewMatrix);
}

GLSLMeshGeometryProcessor::GLSLMeshGeometryProcessor(bool hasTexCoords, bool hasColors,
                                                     bool hasCoverage, PMColor color,
                                                     const Matrix& viewMatrix)
    : MeshGeometryProcessor(hasTexCoords, hasColors, hasCoverage, color, viewMatrix) {
}

void GLSLMeshGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  // Transform position by view matrix
  std::string positionName = "position";
  vertBuilder->transformPoint(positionName, position.name(), matrixName, false);

  // Handle texture coordinates for FragmentProcessor
  if (hasTexCoords) {
    // User provided texCoords, pass them through
    auto texCoordVar = varyingHandler->addVarying("TexCoord", SLType::Float2);
    vertBuilder->storeVarying(texCoordVar.vsOut(), texCoord.name());
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(texCoord));
  } else {
    // No user texCoords, use position with uvMatrix (like DefaultGeometryProcessor)
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  }

  if (hasColors) {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->storeVarying(colorVar.vsOut(), color.name());

    // Output vertex color (will be modulated by FragmentProcessor if texCoords present)
    fragBuilder->storeColor(args.outputColor, colorVar.fsIn());
  } else {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->storeColor(args.outputColor, colorName);
  }

  // Handle coverage for anti-aliasing
  if (hasCoverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->storeVarying(coverageVar.vsOut(), coverage.name());
    fragBuilder->storeCoverage(args.outputCoverage, coverageVar.fsIn(), true);
  } else {
    // No coverage for mesh (no anti-aliasing)
    fragBuilder->storeCoverage(args.outputCoverage, "1.0", true);
  }

  vertBuilder->storePosition(positionName);
}

void GLSLMeshGeometryProcessor::setData(UniformData* vertexUniformData,
                                        UniformData* fragmentUniformData,
                                        FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("Matrix", viewMatrix);

  // For mesh, position is already in local coordinate space (not pre-transformed like shape).
  // Both hasTexCoords and !hasTexCoords cases use identity since coordinates don't need
  // additional transformation - they're already suitable for shader sampling.
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);

  if (!hasColors) {
    fragmentUniformData->setData("Color", commonColor);
  }
}

}  // namespace tgfx
