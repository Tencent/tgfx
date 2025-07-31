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

#include "GLAtlasTextGeometryProcessor.h"
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
PlacementPtr<AtlasTextGeometryProcessor> AtlasTextGeometryProcessor::Make(
    BlockBuffer* buffer, std::shared_ptr<TextureProxy> textureProxy, AAType aa,
    std::optional<Color> commonColor) {
  return buffer->make<GLAtlasTextGeometryProcessor>(std::move(textureProxy), aa, commonColor);
}

GLAtlasTextGeometryProcessor::GLAtlasTextGeometryProcessor(
    std::shared_ptr<TextureProxy> textureProxy, AAType aa, std::optional<Color> commonColor)
    : AtlasTextGeometryProcessor(std::move(textureProxy), aa, commonColor) {
}

void GLAtlasTextGeometryProcessor::emitCode(EmitArgs& args) const {
  auto* vertBuilder = args.vertBuilder;
  auto* fragBuilder = args.fragBuilder;
  auto* varyingHandler = args.varyingHandler;
  auto* uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto atlasName =
      uniformHandler->addUniform(ShaderFlags::Vertex, SLType::Float2, atlasSizeUniformName);

  auto samplerVarying = varyingHandler->addVarying("textureCoords", SLType::Float2);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, position.asShaderVar());
  auto uvName = maskCoord.asShaderVar().name();
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
        args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float4, "Color");
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), color.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  }
  auto texture = textureProxy->getTexture();
  DEBUG_ASSERT(texture != nullptr);
  DEBUG_ASSERT(texture->getSampler() != nullptr);
  auto samplerHandle = uniformHandler->addSampler(texture->getSampler(), "TextureSampler");
  fragBuilder->codeAppend("vec4 color = ");
  fragBuilder->appendTextureLookup(samplerHandle, samplerVarying.vsOut());
  fragBuilder->codeAppend(";");
  if (texture->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = vec4(color.a);", args.outputCoverage.c_str());
  } else {
    fragBuilder->codeAppendf("%s = clamp(vec4(color.rgb/color.a, 1.0), 0.0, 1.0);",
                             args.outputColor.c_str());
    fragBuilder->codeAppendf("%s = vec4(color.a);", args.outputCoverage.c_str());
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(position.name());
}

void GLAtlasTextGeometryProcessor::setData(UniformBuffer* uniformBuffer,
                                           FPCoordTransformIter* transformIter) const {
  auto atlasSizeInv = textureProxy->getTexture()->getTextureCoord(1.f, 1.f);
  uniformBuffer->setData(atlasSizeUniformName, atlasSizeInv);
  setTransformDataHelper(Matrix::I(), uniformBuffer, transformIter);
  if (commonColor.has_value()) {
    uniformBuffer->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
