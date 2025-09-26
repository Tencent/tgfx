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
    BlockBuffer* buffer, std::shared_ptr<TextureProxy> textureProxy, AAType aa,
    std::optional<Color> commonColor, const SamplingOptions& sampling) {
  return buffer->make<GLSLAtlasTextGeometryProcessor>(std::move(textureProxy), aa, commonColor,
                                                      sampling);
}

GLSLAtlasTextGeometryProcessor::GLSLAtlasTextGeometryProcessor(
    std::shared_ptr<TextureProxy> textureProxy, AAType aa, std::optional<Color> commonColor,
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
  auto uvName = maskCoord.name();
  vertBuilder->codeAppendf("%s = %s * %s;", samplerVarying.vsOut().c_str(), uvName.c_str(),
                           atlasName.c_str());

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), coverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), color.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  }
  auto textureView = textureProxy->getTextureView();
  DEBUG_ASSERT(textureView != nullptr);
  DEBUG_ASSERT(textureView->getTexture() != nullptr);
  auto samplerHandle = uniformHandler->addSampler(textureView->getTexture(), "TextureSampler");
  fragBuilder->codeAppend("vec4 color = ");
  fragBuilder->appendTextureLookup(samplerHandle, samplerVarying.vsOut());
  fragBuilder->codeAppend(";");
  if (textureView->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = vec4(color.a);", args.outputCoverage.c_str());
  } else {
    fragBuilder->codeAppendf("%s = clamp(vec4(color.rgb/color.a, 1.0), 0.0, 1.0);",
                             args.outputColor.c_str());
    fragBuilder->codeAppendf("%s = vec4(color.a);", args.outputCoverage.c_str());
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(position.name());
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
