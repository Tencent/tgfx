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

#include "GLSLShapeInstancedGeometryProcessor.h"

namespace tgfx {
PlacementPtr<ShapeInstancedGeometryProcessor> ShapeInstancedGeometryProcessor::Make(
    BlockAllocator* allocator, PMColor color, int width, int height, AAType aa, bool hasColors,
    bool hasShader, const Matrix& uvMatrix, const Matrix& stateMatrix) {
  return allocator->make<GLSLShapeInstancedGeometryProcessor>(color, width, height, aa, hasColors,
                                                              hasShader, uvMatrix, stateMatrix);
}

GLSLShapeInstancedGeometryProcessor::GLSLShapeInstancedGeometryProcessor(
    PMColor color, int width, int height, AAType aa, bool hasColors, bool hasShader,
    const Matrix& uvMatrix, const Matrix& stateMatrix)
    : ShapeInstancedGeometryProcessor(color, width, height, aa, hasColors, hasShader, uvMatrix,
                                      stateMatrix) {
}

void GLSLShapeInstancedGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  // Step 1: uvMatrix transforms tessellation-space position back to local space.
  auto uvMatrixName =
      uniformHandler->addUniform("UVMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  vertBuilder->codeAppendf("highp vec2 local = (%s * vec3(%s, 1.0)).xy;", uvMatrixName.c_str(),
                           position.name().c_str());

  // Step 2: transform local coords to device space, then add per-instance offset.
  // The offset is already in device space (computed as the difference of translated positions),
  // so it must be applied after the stateMatrix transform, not before.
  // P_device = stateMatrix * vec3(local, 1.0) + offset
  auto stateMatrixName =
      uniformHandler->addUniform("StateMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->codeAppendf("highp vec2 %s = (%s * vec3(local, 1.0)).xy + %s;", positionName.c_str(),
                           stateMatrixName.c_str(), offset.name().c_str());

  // Emit UV transforms using unshifted local coords. All FP coord transforms (both color shader
  // and mask coverage) use 'local' without offset, because: (1) mask texture is rasterized at a
  // fixed position shared by all instances, (2) shader patterns are defined in local space and
  // are the same for all instances. The offset only affects device-space position.
  ShaderVar localVar("local", SLType::Float2);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, localVar);

  // Coverage varying for AA.
  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), coverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }

  // Color: per-instance color or uniform color.
  if (hasColors) {
    auto colorVar = varyingHandler->addVarying("InstanceColor", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), instanceColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  } else {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  }

  // Emit the vertex position to NDC.
  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLShapeInstancedGeometryProcessor::setData(UniformData* vertexUniformData,
                                                  UniformData* fragmentUniformData,
                                                  FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("UVMatrix", uvMatrix);
  vertexUniformData->setData("StateMatrix", stateMatrix);
  if (!hasColors) {
    fragmentUniformData->setData("Color", color);
  }
  // For instanced rendering, emitTransforms uses local coords directly. The coord transform
  // matrices only need the FP's own transform since uvMatrix already recovers local coords.
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
}
}  // namespace tgfx
