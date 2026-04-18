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
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  if (args.gpUniforms) {
    args.gpUniforms->add("Matrix", matrixName);
  }

  // Handle texture coordinates for FragmentProcessor
  if (hasTexCoords) {
    auto texCoordVar = varyingHandler->addVarying("TexCoord", SLType::Float2);
    if (args.gpVaryings) {
      args.gpVaryings->add("TexCoord", texCoordVar.fsIn());
    }
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(texCoord));
  } else {
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  }

  if (hasColors) {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorVar.fsIn());
    }
  } else {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  }

  // Handle coverage for anti-aliasing
  if (hasCoverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageVar.fsIn());
    }
  }
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
