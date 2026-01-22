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
                                                                PMColor color,
                                                                const Matrix& viewMatrix) {
  return allocator->make<GLSLMeshGeometryProcessor>(hasTexCoords, hasColors, color, viewMatrix);
}

GLSLMeshGeometryProcessor::GLSLMeshGeometryProcessor(bool hasTexCoords, bool hasColors,
                                                     PMColor color, const Matrix& viewMatrix)
    : MeshGeometryProcessor(hasTexCoords, hasColors, color, viewMatrix) {
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
  vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                           matrixName.c_str(), position.name().c_str());

  // Handle texture coordinates
  if (hasTexCoords) {
    auto texCoordVar = varyingHandler->addVarying("TexCoord", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", texCoordVar.vsOut().c_str(), texCoord.name().c_str());

    // Emit transforms for FragmentProcessor texture sampling
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(texCoord));
  }

  if (hasColors) {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), color.name().c_str());

    // Output vertex color (will be modulated by FragmentProcessor if texCoords present)
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  } else {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  }

  // No coverage for mesh (no anti-aliasing)
  fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());

  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLMeshGeometryProcessor::setData(UniformData* vertexUniformData,
                                        UniformData* fragmentUniformData,
                                        FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("Matrix", viewMatrix);

  if (hasTexCoords) {
    // Use identity matrix since texCoords are in pixel space, CoordTransform handles normalization
    setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  }

  if (!hasColors) {
    fragmentUniformData->setData("Color", commonColor);
  }
}

}  // namespace tgfx
