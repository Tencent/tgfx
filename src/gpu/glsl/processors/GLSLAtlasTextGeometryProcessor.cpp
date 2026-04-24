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
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto atlasName =
      uniformHandler->addUniform(atlasSizeUniformName, UniformFormat::Float2, ShaderStage::Vertex);
  if (args.gpUniforms) {
    args.gpUniforms->add("atlasSizeInv", atlasName);
  }

  auto samplerVarying = varyingHandler->addVarying("textureCoords", SLType::Float2);
  registerCoordTransforms(args, varyingHandler, uniformHandler);
  if (args.gpVaryings) {
    args.gpVaryings->add("textureCoords", samplerVarying.fsIn());
  }

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageVar.fsIn());
    }
  }

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorVar.fsIn());
    }
  }

  auto textureView = textureProxy->getTextureView();
  DEBUG_ASSERT(textureView != nullptr);
  DEBUG_ASSERT(textureView->getTexture() != nullptr);
  auto samplerHandle = uniformHandler->addSampler(textureView->getTexture(), "TextureSampler");
  auto samplerName = uniformHandler->getSamplerVariable(samplerHandle).name();

  // Sample the atlas into a shared FS local so that buildColorCallExpr / buildCoverageCallExpr
  // can reference it via `_atlasTexColor`. The actual swizzle (RRRR for ALPHA_8 atlases, RGBA
  // otherwise) and sampler-type overload (sampler2D vs sampler2DRect on macOS Rectangle
  // textures) are resolved inside TGFX_AtlasText_SampleAtlas — defined in
  // atlas_text_geometry.frag.glsl and injected into the FS by ModularProgramBuilder. The only
  // runtime string assembly here is the single formatted call site, because the sampler name
  // carries the name-mangling suffix assigned by the uniform handler.
  fragBuilder->codeAppendf("vec4 _atlasTexColor = TGFX_AtlasText_SampleAtlas(%s, %s);",
                           samplerName.c_str(), samplerVarying.fsIn().c_str());
  if (args.gpUniforms) {
    args.gpUniforms->add("atlasTexColor", "_atlasTexColor");
  }
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
