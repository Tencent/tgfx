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
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  // Step 1: uvMatrix transforms tessellation-space position back to local space.
  auto uvMatrixName =
      uniformHandler->addUniform("UVMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  if (args.gpUniforms) {
    args.gpUniforms->add("UVMatrix", uvMatrixName);
  }

  // Step 2: transform tessellation-space position directly to device space, then add per-instance
  // offset. viewMatrix = stateMatrix * uvMatrix, which maps tessellation space to device space in
  // one step. The offset is already in device space (computed as the difference of translated
  // positions), so it must be applied after the viewMatrix transform.
  auto viewMatrixName =
      uniformHandler->addUniform("ViewMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  if (args.gpUniforms) {
    args.gpUniforms->add("ViewMatrix", viewMatrixName);
  }

  // Coverage varying for AA.
  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageVar.fsIn());
    }
  }

  // Color: per-instance color or opaque white (overridden by shader FP).
  if (hasColors) {
    auto colorVar = varyingHandler->addVarying("InstanceColor", SLType::Float4);
    if (args.gpVaryings) {
      args.gpVaryings->add("InstanceColor", colorVar.fsIn());
    }
  }

  // Local-space coord varying (uvMatrix * position). Written by TGFX_ShapeInstancedGP_VS and
  // consumed by emitCoordTransformCode as the uv input for FP coord transforms. All FP coord
  // transforms (both color shader and mask coverage) use local coords without offset, because:
  // (1) mask texture is rasterized at a fixed position shared by all instances, (2) shader
  // patterns are defined in local space and are the same for all instances. The offset only
  // affects device-space position.
  auto localCoordVarying = varyingHandler->addVarying("LocalCoord", SLType::Float2);
  if (args.gpVaryings) {
    args.gpVaryings->add("LocalCoord", localCoordVarying.vsOut());
  }

  registerCoordTransforms(args, varyingHandler, uniformHandler);
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
