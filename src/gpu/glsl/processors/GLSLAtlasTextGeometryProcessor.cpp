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
#include "GeneratedOperators.h"

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

  Varying coverageVar = {};
  if (aa == AAType::Coverage) {
    coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->storeVarying(coverageVar.vsOut(), coverage.name());
    fragBuilder->storeCoverage(args.outputCoverage, coverageVar.fsIn(), true);
  } else {
    fragBuilder->storeCoverage(args.outputCoverage, "vec4(1.0)", false);
  }

  std::string colorUniformName = {};
  Varying colorVar = {};
  if (commonColor.has_value()) {
    colorUniformName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->storeColor(args.outputColor, colorUniformName);
  } else {
    colorVar = varyingHandler->addVarying("Color", SLType::Float4);
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

  // The IR route: the atlas coordinate multiply, the texture read and the two text operators as
  // SSA values. Only the fast path has an IR form: the fallback branch is the legacy text path
  // and keeps its text emission.
  if (args.irBuilder != nullptr && !sampleEntry.empty() && !coverageEntry.empty() &&
      readSwizzle >= 0 && (textureView->isAlphaOnly() || !colorEntry.empty())) {
    auto ir = args.irBuilder;
    // atlasCoord = maskCoord * atlasSize (the varying the texture read samples through).
    const auto atlasValue =
        ir->InternValue(atlasName, SLType::Float2, Precision::High, IRValueKind::Uniform);
    const auto maskCoordValue = ir->InternValue(maskCoord.name(), SLType::Float2, Precision::High,
                                               IRValueKind::Attribute);
    const auto atlasCoord =
        ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, maskCoordValue, atlasValue);
    const auto textureCoordsVarying =
        ir->AddVarying({samplerVarying.name(), SLType::Float2, Precision::High, 0, false});
    ir->StoreVarying({textureCoordsVarying, ""}, atlasCoord);
    if (aa == AAType::Coverage) {
      emitVaryingIR(args, coverageVar, ShaderVar(coverage));
    }
    if (!commonColor.has_value()) {
      emitVaryingIR(args, colorVar, ShaderVar(color));
    }
    emitPositionIR(args, ShaderVar(position));
    ir->SetStage(ShaderStage::Fragment);
    if (commonColor.has_value()) {
      const auto colorValue = ir->InternValue(colorUniformName, SLType::Float4, Precision::High,
                                              IRValueKind::Uniform);
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, colorValue);
    } else {
      const auto colorValue = ir->InternValue(colorVar.fsIn(), SLType::Float4, Precision::High,
                                              IRValueKind::Varying);
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, colorValue);
    }
    const auto samplerVariableName = uniformHandler->getSamplerVariable(samplerHandle).name();
    const auto samplerValue = ir->InternValue(samplerVariableName, SLType::Texture2DSampler,
                                              Precision::Default, IRValueKind::Sampler);
    if (samplerValue != INVALID_VALUE_ID) {
      const auto textureCoordsIn = ir->InternValue(samplerVarying.fsIn(), SLType::Float2,
                                                   Precision::High, IRValueKind::Varying);
      const auto textTexel = ir->CreateTextureSample(samplerValue, textureCoordsIn,
                                                     SLType::Float4, "textTexel",
                                                     SLType::Texture2DSampler, readSwizzle);
      const auto textCoverage = TextCoverageFromAlphaOp::Emit(ir, uniformHandler, nullptr,
                                                              textTexel);
      const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                   Precision::High, IRValueKind::Local);
      ir->StoreOutput(coverageOutput, textCoverage);
      if (!textureView->isAlphaOnly()) {
        const auto textColor = TextColorUnpremulOp::Emit(ir, uniformHandler, nullptr, textTexel);
        const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4,
                                                 Precision::High, IRValueKind::Local);
        ir->StoreOutput(colorOutput, textColor);
      }
    }
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
