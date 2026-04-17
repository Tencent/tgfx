/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GLSLAtlasTextGeometryProcessor.h"

namespace tgfx {
PlacementPtr<AtlasTextGeometryProcessor> AtlasTextGeometryProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> textureProxy, AAType aa,
    std::optional<PMColor> commonColor, const SamplingOptions& sampling) {
  return allocator->make<GLSLAtlasTextGeometryProcessor>(std::move(textureProxy), aa, commonColor,
                                                         sampling);
}

GLSLAtlasTextGeometryProcessor::GLSLAtlasTextGeometryProcessor(
    std::shared_ptr<TextureProxy> textureProxy, AAType aa, std::optional<PMColor> commonColor,
    const SamplingOptions& sampling)
    : AtlasTextGeometryProcessor(std::move(textureProxy), aa, commonColor, sampling) {
}

void GLSLAtlasTextGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto atlasName =
      uniformHandler->addUniform(atlasSizeUniformName, UniformFormat::Float2, ShaderStage::Vertex);

  auto samplerVarying = varyingHandler->addVarying("textureCoords", SLType::Float2);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  if (args.gpVaryings) {
    args.gpVaryings->add("textureCoords", samplerVarying.fsIn());
  }

  std::string coverageFsIn;
  std::string coverageVsOut;
  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    coverageVsOut = coverageVar.vsOut();
    coverageFsIn = coverageVar.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageFsIn);
    }
  }

  std::string colorFsIn;
  std::string colorVsOut;
  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    colorFsIn = colorName;
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    colorVsOut = colorVar.vsOut();
    colorFsIn = colorVar.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorFsIn);
    }
  }

  std::string positionName = "position";
  static const std::string kAtlasTextGPVert = R"GLSL(
void TGFX_AtlasTextGP_VS(vec2 inPosition, vec2 inMaskCoord, vec2 atlasSizeInv,
#ifdef TGFX_GP_ATLAS_COVERAGE_AA
                          float inCoverage, out float vCoverage,
#endif
#ifndef TGFX_GP_ATLAS_COMMON_COLOR
                          vec4 inColor, out vec4 vColor,
#endif
                          out vec2 vTextureCoords, out vec2 position) {
    vTextureCoords = inMaskCoord * atlasSizeInv;
#ifdef TGFX_GP_ATLAS_COVERAGE_AA
    vCoverage = inCoverage;
#endif
#ifndef TGFX_GP_ATLAS_COMMON_COLOR
    vColor = inColor;
#endif
    position = inPosition;
}
)GLSL";
  vertBuilder->addFunction(kAtlasTextGPVert);
  vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
  std::string call = "TGFX_AtlasTextGP_VS(" + std::string(position.name()) + ", " +
                     std::string(maskCoord.name()) + ", " + atlasName;
  if (aa == AAType::Coverage) {
    call += ", " + std::string(coverage.name()) + ", " + coverageVsOut;
  }
  if (!commonColor.has_value()) {
    call += ", " + std::string(color.name()) + ", " + colorVsOut;
  }
  call += ", " + samplerVarying.vsOut() + ", " + positionName + ");";
  vertBuilder->codeAppend(call);

  auto textureView = textureProxy->getTextureView();
  DEBUG_ASSERT(textureView != nullptr);
  DEBUG_ASSERT(textureView->getTexture() != nullptr);
  auto samplerHandle = uniformHandler->addSampler(textureView->getTexture(), "TextureSampler");

  // Modular path: emit the texture lookup (with correct swizzle) as a shared FS variable,
  // then record its name so buildColorCallExpr/buildCoverageCallExpr can reference it.
  fragBuilder->codeAppend("vec4 _atlasTexColor = ");
  fragBuilder->appendTextureLookup(samplerHandle, samplerVarying.fsIn());
  fragBuilder->codeAppend(";\n");
  if (args.gpUniforms) {
    args.gpUniforms->add("atlasTexColor", "_atlasTexColor");
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLAtlasTextGeometryProcessor::setData(UniformData* vertexUniformData,
                                             UniformData* fragmentUniformData,
                                             FPCoordTransformIter* transformIter) const {
  auto atlasSizeInv = textureProxy->getTextureView()->getTextureCoord(1.f, 1.f);
  vertexUniformData->setData(atlasSizeUniformName, atlasSizeInv);
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
