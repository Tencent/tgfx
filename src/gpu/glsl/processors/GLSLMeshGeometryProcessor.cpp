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

#include "gpu/ir/ProgramIREmit.h"
#include "GLSLMeshGeometryProcessor.h"
#include "gpu/VertexShaderBuilder.h"

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

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  const std::string positionName = "position";

  // Handle texture coordinates for FragmentProcessor
  Varying texCoordVar = {};
  if (hasTexCoords) {
    // User provided texCoords, pass them through
    texCoordVar = varyingHandler->addVarying("TexCoord", SLType::Float2);
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(texCoord));
  } else {
    // No user texCoords, use position with uvMatrix (like DefaultGeometryProcessor)
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  }

  std::string colorUniformName = {};
  Varying colorVar = {};
  if (hasColors) {
    colorVar = varyingHandler->addVarying("Color", SLType::Float4);
  } else {
    colorUniformName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  }

  // Handle coverage for anti-aliasing
  Varying coverageVar = {};
  if (hasCoverage) {
    coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
  } else {
  }

  // The IR route: the transformed position and every optional varying as SSA values.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    if (hasTexCoords) {
      emitVaryingIR(args, texCoordVar, ShaderVar(texCoord));
    }
    if (hasColors) {
      emitVaryingIR(args, colorVar, ShaderVar(color));
    }
    if (hasCoverage) {
      emitVaryingIR(args, coverageVar, ShaderVar(coverage));
    }
    emitPositionTransformIR(ir, position.name(), SLType::Float2, matrixName);
    const auto one = ir->CreateConstant(1.0f);
    ir->SetStage(ShaderStage::Fragment);
    if (hasColors) {
    emitOutputColorIR(ir, colorVar.fsIn(), true, args.outputColor);
    } else {
    emitOutputColorIR(ir, colorUniformName, false, args.outputColor);
    }
    if (hasCoverage) {
      const auto coverageIn = ir->InternValue(coverageVar.fsIn(), SLType::Float, Precision::High,
                                              IRValueKind::Varying);
    emitOutputCoverageIR(ir, coverageIn, false, args.outputCoverage);
    } else {
    emitOutputCoverageIR(ir, one, false, args.outputCoverage);
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
