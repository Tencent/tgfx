/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GLSLTiledTextureEffect.h"
#include "gpu/SamplerState.h"
#include "gpu/ShaderCaps.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

namespace {
// The per-axis mode the shared sampler receives: 0 none/clamp/border, 1 repeat, 2 mipmap seam,
// 3 mirror.
int AxisModeOf(TiledTextureEffect::ShaderMode mode) {
  switch (mode) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::Clamp:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
      return 0;
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
      return 1;
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
      return 2;
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
      return 3;
  }
  return 0;
}

// The coordinates the shader works in are normalized by the Dimension uniform (see emitCode), so a
// sample re-applies it. Empty dimensions (rectangle textures) leave the expression alone.
std::string ScaledCoord(const std::string& coord, const std::string& dimensions) {
  if (dimensions.empty()) {
    return coord;
  }
  return "(" + coord + ") * " + dimensions;
}
}  // namespace
PlacementPtr<FragmentProcessor> TiledTextureEffect::Make(BlockAllocator* allocator,
                                                         std::shared_ptr<TextureProxy> proxy,
                                                         const SamplingArgs& args,
                                                         const Matrix* uvMatrix, bool forceAsMask) {
  if (proxy == nullptr) {
    return nullptr;
  }
  if (args.tileModeX == TileMode::Clamp && args.tileModeY == TileMode::Clamp) {
    return TextureEffect::Make(allocator, std::move(proxy), args, uvMatrix, forceAsMask);
  }
  auto matrix = uvMatrix ? *uvMatrix : Matrix::I();
  SamplerState samplerState(args.tileModeX, args.tileModeY, args.sampling);
  auto isAlphaOnly = proxy->isAlphaOnly();
  PlacementPtr<FragmentProcessor> processor = allocator->make<GLSLTiledTextureEffect>(
      std::move(proxy), samplerState, args.constraint, matrix, args.sampleArea);
  if (forceAsMask && !isAlphaOnly) {
    processor = FragmentProcessor::MulInputByChildAlpha(allocator, std::move(processor));
  }
  return processor;
}

GLSLTiledTextureEffect::GLSLTiledTextureEffect(std::shared_ptr<TextureProxy> proxy,
                                               const SamplerState& samplerState,
                                               SrcRectConstraint constraint, const Matrix& uvMatrix,
                                               const std::optional<Rect>& subset)
    : TiledTextureEffect(std::move(proxy), samplerState, constraint, uvMatrix, subset) {
}

bool GLSLTiledTextureEffect::ShaderModeRequiresUnormCoord(TiledTextureEffect::ShaderMode mode) {
  switch (mode) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::Clamp:
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
      return false;
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
      return true;
  }
  return false;
}

bool GLSLTiledTextureEffect::ShaderModeUsesSubset(TiledTextureEffect::ShaderMode m) {
  switch (m) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::Clamp:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
      return false;
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
      return true;
  }
  return false;
}

bool GLSLTiledTextureEffect::ShaderModeUsesClamp(TiledTextureEffect::ShaderMode m) {
  switch (m) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
      return false;
    case TiledTextureEffect::ShaderMode::Clamp:
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
      return true;
  }
  return false;
}

GLSLTiledTextureEffect::UniformNames GLSLTiledTextureEffect::initUniform(
    EmitArgs& args, const TextureView* textureView, const Sampling& sampling,
    const bool useClamp[2]) const {
  UniformNames names = {};
  auto uniformHandler = args.uniformHandler;
  if (ShaderModeUsesSubset(sampling.shaderModeX) || ShaderModeUsesSubset(sampling.shaderModeY)) {
    names.subsetName =
        uniformHandler->addUniform("Subset", UniformFormat::Float4, ShaderStage::Fragment);
  }
  if (useClamp[0] || useClamp[1]) {
    names.clampName =
        uniformHandler->addUniform("Clamp", UniformFormat::Float4, ShaderStage::Fragment);
  }
  bool unormCoordsRequiredForShaderMode = ShaderModeRequiresUnormCoord(sampling.shaderModeX) ||
                                          ShaderModeRequiresUnormCoord(sampling.shaderModeY);
  bool sampleCoordsMustBeNormalized = textureView->getTexture()->type() != TextureType::Rectangle;
  if (unormCoordsRequiredForShaderMode && sampleCoordsMustBeNormalized) {
    names.dimensionsName =
        uniformHandler->addUniform("Dimension", UniformFormat::Float2, ShaderStage::Fragment);
  }
  return names;
}

bool GLSLTiledTextureEffect::describeTiledRead(EmitArgs& args, SamplerHandle sampler,
                                               TiledReadDescription* description) const {
  auto textureView = getTextureView();
  if (textureView == nullptr || isStrict() || isAlphaOnly() || hasPerspective()) {
    return false;
  }
  const auto samplerVar = args.uniformHandler->getSamplerVariable(sampler);
  if (samplerVar.type() != SLType::Texture2DSampler) {
    return false;
  }
  Sampling sampling(textureView, samplerState, subset);
  if (sampling.shaderModeX == ShaderMode::None && sampling.shaderModeY == ShaderMode::None) {
    return false;  // the plain reader serves it: no tiling to describe
  }
  // The mipmap and clamp-to-border-linear modes sample with an explicit level of detail the blur
  // function does not carry, so those reads keep their own path.
  const auto axisModeX = AxisModeOf(sampling.shaderModeX);
  const auto axisModeY = AxisModeOf(sampling.shaderModeY);
  if (axisModeX == 2 || axisModeY == 2 || sampling.shaderModeX == ShaderMode::ClampToBorderLinear ||
      sampling.shaderModeY == ShaderMode::ClampToBorderLinear ||
      sampling.shaderModeX == ShaderMode::ClampToBorderNearest ||
      sampling.shaderModeY == ShaderMode::ClampToBorderNearest) {
    return false;
  }
  if (sampling.shaderModeX == ShaderMode::Clamp && sampling.shaderModeY == ShaderMode::Clamp) {
    // Both axes clamp: the effect's own emit reads without any tiling math, so the caller must too.
    description->plainRead = true;
    return true;
  }
  const bool useClamp[2] = {ShaderModeUsesClamp(sampling.shaderModeX),
                            ShaderModeUsesClamp(sampling.shaderModeY)};
  auto names = initUniform(args, textureView, sampling, useClamp);
  description->subsetName = names.subsetName;
  description->clampName = names.clampName;
  description->dimensionName = names.dimensionsName;
  if (args.irBuilder != nullptr) {
    if (!names.subsetName.empty()) {
      description->subset = args.irBuilder->InternValue(names.subsetName, SLType::Float4,
                                                        Precision::High, IRValueKind::Uniform);
    }
    if (!names.clampName.empty()) {
      description->clamp = args.irBuilder->InternValue(names.clampName, SLType::Float4,
                                                       Precision::High, IRValueKind::Uniform);
    }
    if (!names.dimensionsName.empty()) {
      description->dimensions = args.irBuilder->InternValue(names.dimensionsName, SLType::Float2,
                                                            Precision::High, IRValueKind::Uniform);
    }
  }
  // The shared mapping takes the shader-mode enumerators themselves, not the axis-coordinate
  // encoding the emit path uses for its literals.
  description->shaderModeX = static_cast<int>(sampling.shaderModeX);
  description->shaderModeY = static_cast<int>(sampling.shaderModeY);
  const bool repeatX = sampling.shaderModeX == ShaderMode::RepeatLinearNone ||
                       sampling.shaderModeX == ShaderMode::RepeatLinearMipmap;
  const bool repeatY = sampling.shaderModeY == ShaderMode::RepeatLinearNone ||
                       sampling.shaderModeY == ShaderMode::RepeatLinearMipmap;
  const bool borderLinearX = sampling.shaderModeX == ShaderMode::ClampToBorderLinear;
  const bool borderLinearY = sampling.shaderModeY == ShaderMode::ClampToBorderLinear;
  const bool borderNearestX = sampling.shaderModeX == ShaderMode::ClampToBorderNearest;
  const bool borderNearestY = sampling.shaderModeY == ShaderMode::ClampToBorderNearest;
  if (repeatX) {
    description->flags |= TiledFlags(TiledReadFlag::RepeatX);
  }
  if (repeatY) {
    description->flags |= TiledFlags(TiledReadFlag::RepeatY);
  }
  if (borderLinearX) {
    description->flags |= TiledFlags(TiledReadFlag::BorderLinearX);
  }
  if (borderLinearY) {
    description->flags |= TiledFlags(TiledReadFlag::BorderLinearY);
  }
  if (borderNearestX) {
    description->flags |= TiledFlags(TiledReadFlag::BorderNearestX);
  }
  if (borderNearestY) {
    description->flags |= TiledFlags(TiledReadFlag::BorderNearestY);
  }
  description->seamBlend = repeatX || repeatY;
  description->borderPass = borderLinearX || borderLinearY;
  return true;
}

void GLSLTiledTextureEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    // emit a transparent color as the output color.
    fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
    return;
  }
  auto texCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  if (args.coordFunc) {
    texCoordName = args.coordFunc(texCoordName);
  }
  Sampling sampling(textureView, samplerState, subset);
  const auto& sampler = (*args.textureSamplers)[0];
  const auto samplerVar = args.uniformHandler->getSamplerVariable(sampler);
  const auto samplerName = samplerVar.name();
  ValueID textureValue = INVALID_VALUE_ID;
  auto makeCoordValue = [&](const std::string& name) -> ValueID {
    if (args.irBuilder == nullptr) {
      return INVALID_VALUE_ID;
    }
    const auto* existing = args.irBuilder->FindValue(name);
    if (existing != nullptr) {
      if (existing->type == SLType::Float2) {
        return existing->id;
      }
      if (existing->type == SLType::Float3 || existing->type == SLType::Float4) {
        return args.irBuilder->CreateSwizzle(existing->id, SLType::Float2, "xy");
      }
    }
    return args.irBuilder->InternValue(name, SLType::Float2, Precision::High, IRValueKind::Local);
  };
  if (sampling.shaderModeX == TiledTextureEffect::ShaderMode::None &&
      sampling.shaderModeY == TiledTextureEffect::ShaderMode::None) {
    fragBuilder->codeAppendf("%s = ", args.outputColor.c_str());
    fragBuilder->appendTextureLookup(sampler, texCoordName);
    fragBuilder->codeAppend(";");
    if (args.irBuilder != nullptr && args.samplerValue(sampler) != INVALID_VALUE_ID) {
      const auto coordValue = makeCoordValue(texCoordName);
      textureValue =
          args.irBuilder->CreateTextureSample(args.samplerValue(sampler), coordValue,
                                              SLType::Float4, args.outputColor, samplerVar.type());
    }
  } else {
    bool useClamp[2] = {ShaderModeUsesClamp(sampling.shaderModeX),
                        ShaderModeUsesClamp(sampling.shaderModeY)};
    auto names = initUniform(args, textureView, sampling, useClamp);
    const std::string typeSuffix = samplerVar.type() == SLType::Texture2DRectSampler ? "Rect" : "";
    const std::string subsetArg = names.subsetName.empty() ? "vec4(0.0)" : names.subsetName;
    const std::string clampArg = names.clampName.empty() ? "vec4(0.0)" : names.clampName;
    const int modeX = AxisModeOf(sampling.shaderModeX);
    const int modeY = AxisModeOf(sampling.shaderModeY);
    const bool mipmapX = modeX == 2;
    const bool mipmapY = modeY == 2;
    // The linear repeat modes need the seam blend whether or not they also carry a mipmap seam,
    // so this pair is judged on the original mode, not on the coordinate-transform class above.
    const bool repeatX = sampling.shaderModeX == TiledTextureEffect::ShaderMode::RepeatLinearNone ||
                         sampling.shaderModeX == TiledTextureEffect::ShaderMode::RepeatLinearMipmap;
    const bool repeatY = sampling.shaderModeY == TiledTextureEffect::ShaderMode::RepeatLinearNone ||
                         sampling.shaderModeY == TiledTextureEffect::ShaderMode::RepeatLinearMipmap;
    const bool borderLinearX =
        sampling.shaderModeX == TiledTextureEffect::ShaderMode::ClampToBorderLinear;
    const bool borderLinearY =
        sampling.shaderModeY == TiledTextureEffect::ShaderMode::ClampToBorderLinear;
    const bool borderNearestX =
        sampling.shaderModeX == TiledTextureEffect::ShaderMode::ClampToBorderNearest;
    const bool borderNearestY =
        sampling.shaderModeY == TiledTextureEffect::ShaderMode::ClampToBorderNearest;

    auto axisCoord = fragBuilder->appendFunction("tgfxTiledAxisCoord");
    auto axisClamp = fragBuilder->appendFunction("tgfxTiledAxisClamp");
    if (axisCoord.empty() || axisClamp.empty()) {
      return;
    }
    fragBuilder->aliasValue("inCoord", texCoordName, "highp vec2");
    if (!names.dimensionsName.empty()) {
      fragBuilder->codeAppendf("inCoord /= %s;", names.dimensionsName.c_str());
    }
    fragBuilder->codeAppendf("highp vec4 axisX = %s(inCoord.x, %s.x, %s.z, %d);", axisCoord.c_str(),
                             subsetArg.c_str(), subsetArg.c_str(), modeX);
    fragBuilder->codeAppendf("highp vec4 axisY = %s(inCoord.y, %s.y, %s.w, %d);", axisCoord.c_str(),
                             subsetArg.c_str(), subsetArg.c_str(), modeY);
    fragBuilder->codeAppend("highp vec2 subsetCoord = vec2(axisX.x, axisY.x);");
    fragBuilder->codeAppendf(
        "highp vec2 clampedCoord = vec2(%s(axisX.x, %s.x, %s.z, %d), "
        "%s(axisY.x, %s.y, %s.w, %d));",
        axisClamp.c_str(), clampArg.c_str(), clampArg.c_str(), useClamp[0] ? 1 : 0,
        axisClamp.c_str(), clampArg.c_str(), clampArg.c_str(), useClamp[1] ? 1 : 0);
    if (constraint == SrcRectConstraint::Strict) {
      std::string strictSubset = args.inputSubset;
      if (!names.dimensionsName.empty()) {
        fragBuilder->aliasValue("extraSubset", strictSubset, "highp vec4");
        fragBuilder->codeAppendf("extraSubset.xy /= %s;", names.dimensionsName.c_str());
        fragBuilder->codeAppendf("extraSubset.zw /= %s;", names.dimensionsName.c_str());
        strictSubset = "extraSubset";
      }
      fragBuilder->codeAppendf("clampedCoord = tgfxClampSubset(clampedCoord, %s);",
                               strictSubset.c_str());
    }
    if (mipmapX) {
      fragBuilder->codeAppendf("axisX.y = %s(axisX.y, %s.x, %s.z, %d);", axisClamp.c_str(),
                               clampArg.c_str(), clampArg.c_str(), useClamp[0] ? 1 : 0);
    }
    if (mipmapY) {
      fragBuilder->codeAppendf("axisY.y = %s(axisY.y, %s.y, %s.w, %d);", axisClamp.c_str(),
                               clampArg.c_str(), clampArg.c_str(), useClamp[1] ? 1 : 0);
    }
    fragBuilder->codeAppend("vec4 textureColor;");
    const std::string clampedScaled = ScaledCoord("clampedCoord", names.dimensionsName);
    if (mipmapX && mipmapY) {
      auto sample = fragBuilder->appendFunction(("tgfxTiledSample4" + typeSuffix).c_str());
      if (sample.empty()) {
        return;
      }
      fragBuilder->codeAppendf(
          "textureColor = %s(%s, %s, %s, %s, %s, axisX.z, axisY.z);", sample.c_str(),
          samplerName.c_str(), clampedScaled.c_str(),
          ScaledCoord("vec2(axisX.y, clampedCoord.y)", names.dimensionsName).c_str(),
          ScaledCoord("vec2(clampedCoord.x, axisY.y)", names.dimensionsName).c_str(),
          ScaledCoord("vec2(axisX.y, axisY.y)", names.dimensionsName).c_str());
    } else if (mipmapX) {
      auto sample = fragBuilder->appendFunction(("tgfxTiledSample2" + typeSuffix).c_str());
      if (sample.empty()) {
        return;
      }
      fragBuilder->codeAppendf(
          "textureColor = %s(%s, %s, %s, axisX.z);", sample.c_str(), samplerName.c_str(),
          clampedScaled.c_str(),
          ScaledCoord("vec2(axisX.y, clampedCoord.y)", names.dimensionsName).c_str());
    } else if (mipmapY) {
      auto sample = fragBuilder->appendFunction(("tgfxTiledSample2" + typeSuffix).c_str());
      if (sample.empty()) {
        return;
      }
      fragBuilder->codeAppendf(
          "textureColor = %s(%s, %s, %s, axisY.z);", sample.c_str(), samplerName.c_str(),
          clampedScaled.c_str(),
          ScaledCoord("vec2(clampedCoord.x, axisY.y)", names.dimensionsName).c_str());
    } else {
      auto sample = fragBuilder->appendFunction(("tgfxTiledSample1" + typeSuffix).c_str());
      if (sample.empty()) {
        return;
      }
      fragBuilder->codeAppendf("textureColor = %s(%s, %s);", sample.c_str(), samplerName.c_str(),
                               clampedScaled.c_str());
    }
    const bool needErrX = repeatX || borderLinearX;
    const bool needErrY = repeatY || borderLinearY;
    if (needErrX) {
      fragBuilder->codeAppend("highp float errX = subsetCoord.x - clampedCoord.x;");
    }
    if (needErrY) {
      fragBuilder->codeAppend("highp float errY = subsetCoord.y - clampedCoord.y;");
    }
    if (repeatX) {
      fragBuilder->codeAppendf("highp float repeatCoordX = errX > 0.0 ? %s.x : %s.z;",
                               clampArg.c_str(), clampArg.c_str());
    }
    if (repeatY) {
      fragBuilder->codeAppendf("highp float repeatCoordY = errY > 0.0 ? %s.y : %s.w;",
                               clampArg.c_str(), clampArg.c_str());
    }
    if (repeatX || repeatY) {
      auto blend = fragBuilder->appendFunction(("tgfxTiledRepeatBlend" + typeSuffix).c_str());
      if (blend.empty()) {
        return;
      }
      const std::string repeatScaled =
          ScaledCoord(std::string("vec2(") + (repeatX ? "repeatCoordX" : "0.0") + ", " +
                          (repeatY ? "repeatCoordY" : "0.0") + ")",
                      names.dimensionsName);
      fragBuilder->codeAppendf("textureColor = %s(%s, textureColor, %s, %s, %s, %s, %d, %d);",
                               blend.c_str(), samplerName.c_str(), clampedScaled.c_str(),
                               repeatScaled.c_str(), needErrX ? "errX" : "0.0",
                               needErrY ? "errY" : "0.0", repeatX ? 1 : 0, repeatY ? 1 : 0);
    }
    if (borderLinearX || borderLinearY) {
      auto border = fragBuilder->appendFunction("tgfxTiledBorderLinear");
      if (border.empty()) {
        return;
      }
      fragBuilder->codeAppendf("textureColor = %s(textureColor, %s, %s, %d, %d);", border.c_str(),
                               needErrX ? "errX" : "0.0", needErrY ? "errY" : "0.0",
                               borderLinearX ? 1 : 0, borderLinearY ? 1 : 0);
    }
    if (borderNearestX || borderNearestY) {
      auto border = fragBuilder->appendFunction("tgfxTiledBorderNearest");
      if (border.empty()) {
        return;
      }
      fragBuilder->codeAppendf("textureColor = %s(textureColor, inCoord, %s, %d, %d);",
                               border.c_str(), subsetArg.c_str(), borderNearestX ? 1 : 0,
                               borderNearestY ? 1 : 0);
    }
    fragBuilder->codeAppendf("%s = textureColor%s;", args.outputColor.c_str(),
                             fragBuilder->getSamplerSwizzleSuffix(sampler).c_str());
    if (args.irBuilder != nullptr && args.samplerValue(sampler) != INVALID_VALUE_ID) {
      const auto zero = args.irBuilder->CreateConstant(0.0f);
      const auto coordValue = makeCoordValue(texCoordName);
      std::vector<ValueID> sampleArgs = {args.samplerValue(sampler), coordValue};
      OperatorID sampleOp = OperatorID::TiledSample1;
      if (mipmapX && mipmapY) {
        sampleArgs.insert(sampleArgs.end(), {coordValue, coordValue, coordValue, zero, zero});
        sampleOp = typeSuffix == "Rect" ? OperatorID::TiledSample4Rect : OperatorID::TiledSample4;
      } else if (mipmapX || mipmapY) {
        sampleArgs.insert(sampleArgs.end(), {coordValue, zero});
        sampleOp = typeSuffix == "Rect" ? OperatorID::TiledSample2Rect : OperatorID::TiledSample2;
      } else {
        sampleOp = typeSuffix == "Rect" ? OperatorID::TiledSample1Rect : OperatorID::TiledSample1;
      }
      textureValue =
          args.irBuilder->CreateCall(sampleOp, SLType::Float4, sampleArgs, args.outputColor);
    }
  }
  if (textureProxy->isAlphaOnly()) {
    args.fragBuilder->codeAppendf("%s = %s.a * %s;", args.outputColor.c_str(),
                                  args.outputColor.c_str(), args.inputColor.c_str());
  } else {
    args.fragBuilder->codeAppendf("%s = %s * %s.a;", args.outputColor.c_str(),
                                  args.outputColor.c_str(), args.inputColor.c_str());
  }
  if (textureValue != INVALID_VALUE_ID && args.irBuilder != nullptr &&
      args.inputValue != INVALID_VALUE_ID && args.outputValue != INVALID_VALUE_ID) {
    const auto op = textureProxy->isAlphaOnly() ? OperatorID::AlphaModulate : OperatorID::MulAlpha;
    const auto result = args.irBuilder->CreateCall(
        op, SLType::Float4, {textureValue, args.inputValue}, args.outputColor);
    args.irBuilder->StoreOutput(args.outputValue, result);
  }
}

void GLSLTiledTextureEffect::onSetData(UniformData* /*vertexUniformData*/,
                                       UniformData* fragmentUniformData) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return;
  }
  Sampling sampling(textureView, samplerState, subset);
  auto hasDimensionUniform = (ShaderModeRequiresUnormCoord(sampling.shaderModeX) ||
                              ShaderModeRequiresUnormCoord(sampling.shaderModeY)) &&
                             textureView->getTexture()->type() != TextureType::Rectangle;
  if (hasDimensionUniform) {
    auto dimensions = textureView->getTextureCoord(1.f, 1.f);
    fragmentUniformData->setData("Dimension", dimensions);
  }
  auto pushRect = [&](Rect subset, const std::string& uni) {
    float rect[4] = {subset.left, subset.top, subset.right, subset.bottom};
    if (textureView->origin() == ImageOrigin::BottomLeft) {
      auto h = static_cast<float>(textureView->height());
      rect[1] = h - rect[1];
      rect[3] = h - rect[3];
      std::swap(rect[1], rect[3]);
    }
    auto type = textureView->getTexture()->type();
    if (!hasDimensionUniform && type != TextureType::Rectangle) {
      auto lt =
          textureView->getTextureCoord(static_cast<float>(rect[0]), static_cast<float>(rect[1]));
      auto rb =
          textureView->getTextureCoord(static_cast<float>(rect[2]), static_cast<float>(rect[3]));
      rect[0] = lt.x;
      rect[1] = lt.y;
      rect[2] = rb.x;
      rect[3] = rb.y;
    }
    fragmentUniformData->setData(uni, rect);
  };
  if (ShaderModeUsesSubset(sampling.shaderModeX) || ShaderModeUsesSubset(sampling.shaderModeY)) {
    pushRect(sampling.shaderSubset, "Subset");
  }
  if (ShaderModeUsesClamp(sampling.shaderModeX) || ShaderModeUsesClamp(sampling.shaderModeY)) {
    pushRect(sampling.shaderClamp, "Clamp");
  }
}
}  // namespace tgfx
