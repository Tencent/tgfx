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

  // Handle texture coordinates for FragmentProcessor
  std::string texCoordVsOut;
  if (hasTexCoords) {
    auto texCoordVar = varyingHandler->addVarying("TexCoord", SLType::Float2);
    texCoordVsOut = texCoordVar.vsOut();
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(texCoord));
  } else {
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  }

  std::string colorFsIn;
  std::string colorVsOut;
  if (hasColors) {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    colorVsOut = colorVar.vsOut();
    colorFsIn = colorVar.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorFsIn);
    }
  } else {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    colorFsIn = colorName;
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  }

  // Handle coverage for anti-aliasing
  std::string coverageFsIn;
  std::string coverageVsOut;
  if (hasCoverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    coverageVsOut = coverageVar.vsOut();
    coverageFsIn = coverageVar.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageFsIn);
    }
  }

  // Transform position by view matrix
  std::string positionName = "position";
  static const std::string kMeshGPVert = R"GLSL(
void TGFX_MeshGP_VS(vec2 inPosition, mat3 matrix,
#ifdef TGFX_GP_MESH_TEX_COORDS
                     vec2 inTexCoord, out vec2 vTexCoord,
#endif
#ifdef TGFX_GP_MESH_VERTEX_COLORS
                     vec4 inColor, out vec4 vColor,
#endif
#ifdef TGFX_GP_MESH_VERTEX_COVERAGE
                     float inCoverage, out float vCoverage,
#endif
                     out vec2 position) {
    position = (matrix * vec3(inPosition, 1.0)).xy;
#ifdef TGFX_GP_MESH_TEX_COORDS
    vTexCoord = inTexCoord;
#endif
#ifdef TGFX_GP_MESH_VERTEX_COLORS
    vColor = inColor;
#endif
#ifdef TGFX_GP_MESH_VERTEX_COVERAGE
    vCoverage = inCoverage;
#endif
}
)GLSL";
  vertBuilder->addFunction(kMeshGPVert);
  vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
  std::string call = "TGFX_MeshGP_VS(" + std::string(position.name()) + ", " + matrixName;
  if (hasTexCoords) {
    call += ", " + std::string(texCoord.name()) + ", " + texCoordVsOut;
  }
  if (hasColors) {
    call += ", " + std::string(color.name()) + ", " + colorVsOut;
  }
  if (hasCoverage) {
    call += ", " + std::string(coverage.name()) + ", " + coverageVsOut;
  }
  call += ", " + positionName + ");";
  vertBuilder->codeAppend(call);

  vertBuilder->emitNormalizedPosition(positionName);
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
