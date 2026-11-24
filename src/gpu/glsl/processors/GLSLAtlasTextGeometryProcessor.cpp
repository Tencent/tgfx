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

  auto atlasName =
      uniformHandler->addUniform(atlasSizeUniformName, UniformFormat::Float2, ShaderStage::Vertex);

  auto samplerVarying = varyingHandler->addVarying("textureCoords", SLType::Float2);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  auto uvName = maskCoord.name();
  // The varying is stored explicitly so a bytecode route sees it produced: the multiply names
  // a local, and the store names the varying.
  vertBuilder->multiplyVec2("atlasCoord", uvName, atlasName, /*declare=*/true);
  vertBuilder->storeVarying(samplerVarying.vsOut(), "atlasCoord");

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->storeVarying(coverageVar.vsOut(), coverage.name());
    fragBuilder->storeCoverage(args.outputCoverage, coverageVar.fsIn(), true);
  } else {
    fragBuilder->storeCoverage(args.outputCoverage, "vec4(1.0)", false);
  }

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->storeColor(args.outputColor, colorName);
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->storeVarying(colorVar.vsOut(), color.name());
    fragBuilder->storeColor(args.outputColor, colorVar.fsIn());
  }
  auto textureView = textureProxy->getTextureView();
  DEBUG_ASSERT(textureView != nullptr);
  DEBUG_ASSERT(textureView->getTexture() != nullptr);
  auto samplerHandle = uniformHandler->addSampler(textureView->getTexture(), "TextureSampler");
  auto sampleEntry = fragBuilder->appendFunction("tgfxTextureSample");
  auto coverageEntry = fragBuilder->appendFunction("tgfxTextCoverageFromAlpha");
  auto colorEntry = fragBuilder->appendFunction("tgfxTextColorUnpremul");
  // The atlas is a mask, so the read carries its sampler's own channel swizzle.
  const int readSwizzle =
      ShaderBuilder::ReadSwizzleCode(uniformHandler->getSamplerSwizzle(samplerHandle));
  if (!sampleEntry.empty() && !coverageEntry.empty() && readSwizzle >= 0 &&
      (textureView->isAlphaOnly() || !colorEntry.empty())) {
    // The texel, the unpremultiplied color and the coverage are all named values, so a bytecode
    // route records the whole block.
    const auto samplerName = uniformHandler->getSamplerVariable(samplerHandle).name();
    fragBuilder->codeAppend("vec4 textTexel;");
    fragBuilder->appendCall(
        "textTexel", sampleEntry,
        {samplerName, samplerVarying.vsOut(), "vec4(0.0)", "0", std::to_string(readSwizzle)});
    fragBuilder->codeAppend("vec4 textCoverage;");
    fragBuilder->appendCall("textCoverage", coverageEntry, {"textTexel"});
    fragBuilder->storeCoverage(args.outputCoverage, "textCoverage", false);
    if (!textureView->isAlphaOnly()) {
      fragBuilder->codeAppend("vec4 textColor;");
      fragBuilder->appendCall("textColor", colorEntry, {"textTexel"});
      fragBuilder->storeColor(args.outputColor, "textColor");
    }
  } else {
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
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->storePosition(position.name());
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
