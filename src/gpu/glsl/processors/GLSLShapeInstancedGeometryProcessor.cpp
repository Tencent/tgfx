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
    BlockAllocator* allocator, int width, int height, AAType aa, bool hasColors,
    const Matrix& uvMatrix, const Matrix& stateMatrix) {
  return allocator->make<GLSLShapeInstancedGeometryProcessor>(width, height, aa, hasColors,
                                                              uvMatrix, stateMatrix);
}

GLSLShapeInstancedGeometryProcessor::GLSLShapeInstancedGeometryProcessor(int width, int height,
                                                                         AAType aa, bool hasColors,
                                                                         const Matrix& uvMatrix,
                                                                         const Matrix& stateMatrix)
    : ShapeInstancedGeometryProcessor(width, height, aa, hasColors, uvMatrix, stateMatrix) {
}

void GLSLShapeInstancedGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  // Step 1: uvMatrix transforms tessellation-space position back to local space.
  auto uvMatrixName =
      uniformHandler->addUniform("UVMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  // Step 2: transform tessellation-space position directly to device space, then add per-instance
  // offset. viewMatrix = stateMatrix * uvMatrix, which maps tessellation space to device space in
  // one step. The offset is already in device space (computed as the difference of translated
  // positions), so it must be applied after the viewMatrix transform.
  auto viewMatrixName =
      uniformHandler->addUniform("ViewMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  // Coverage varying for AA.
  std::string coverageVsOut;
  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    coverageVsOut = coverageVar.vsOut();
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageVar.fsIn());
    }
  }

  // Color: per-instance color or opaque white (overridden by shader FP).
  std::string colorVsOut;
  if (hasColors) {
    auto colorVar = varyingHandler->addVarying("InstanceColor", SLType::Float4);
    colorVsOut = colorVar.vsOut();
    if (args.gpVaryings) {
      args.gpVaryings->add("InstanceColor", colorVar.fsIn());
    }
  }

  // Half-migrated: VS function body lives in shape_instanced_geometry.vert.glsl (injected by
  // ModularProgramBuilder via includeVSModule based on shaderFunctionFile()). We emit the call
  // here because emitTransforms() depends on the `local` variable we compute below from uvMatrix
  // and the attribute position.
  std::string positionName = "position";
  vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
  std::string call = "TGFX_ShapeInstancedGP_VS(" + std::string(position.name()) + ", " +
                     std::string(offset.name()) + ", " + viewMatrixName;
  if (aa == AAType::Coverage) {
    call += ", " + std::string(coverage.name()) + ", " + coverageVsOut;
  }
  if (hasColors) {
    call += ", " + std::string(instanceColor.name()) + ", " + colorVsOut;
  }
  call += ", " + positionName + ");";
  vertBuilder->codeAppend(call);
  vertBuilder->codeAppendf("highp vec2 local = (%s * vec3(%s, 1.0)).xy;", uvMatrixName.c_str(),
                           position.name().c_str());

  // Emit UV transforms using unshifted local coords. All FP coord transforms (both color shader
  // and mask coverage) use 'local' without offset, because: (1) mask texture is rasterized at a
  // fixed position shared by all instances, (2) shader patterns are defined in local space and
  // are the same for all instances. The offset only affects device-space position.
  ShaderVar localVar("local", SLType::Float2);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, localVar);

  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLShapeInstancedGeometryProcessor::setData(UniformData* vertexUniformData, UniformData*,
                                                  FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("UVMatrix", uvMatrix);
  vertexUniformData->setData("ViewMatrix", viewMatrix);
  // For instanced rendering, emitTransforms uses local coords directly. The coord transform
  // matrices only need the FP's own transform since uvMatrix already recovers local coords.
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
}
}  // namespace tgfx
