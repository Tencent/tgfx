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
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> TiledTextureEffect::Make(std::shared_ptr<TextureProxy> proxy,
                                                         const SamplingArgs& args,
                                                         const Matrix* uvMatrix, bool forceAsMask) {
  if (proxy == nullptr) {
    return nullptr;
  }
  if (args.tileModeX == TileMode::Clamp && args.tileModeY == TileMode::Clamp) {
    return TextureEffect::Make(std::move(proxy), args, uvMatrix, forceAsMask);
  }
  auto matrix = uvMatrix ? *uvMatrix : Matrix::I();
  SamplerState samplerState(args.tileModeX, args.tileModeY, args.sampling);
  auto isAlphaOnly = proxy->isAlphaOnly();
  auto drawingBuffer = proxy->getContext()->drawingBuffer();
  PlacementPtr<FragmentProcessor> processor = drawingBuffer->make<GLSLTiledTextureEffect>(
      std::move(proxy), samplerState, args.constraint, matrix, args.sampleArea);
  if (forceAsMask && !isAlphaOnly) {
    processor = FragmentProcessor::MulInputByChildAlpha(drawingBuffer, std::move(processor));
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
}

void GLSLTiledTextureEffect::readColor(EmitArgs& args, const std::string& dimensionsName,
                                       const std::string& coord, const char* out) const {
  std::string normCoord;
  if (!dimensionsName.empty()) {
    normCoord = "(" + coord + ") * " + dimensionsName + "";
  } else {
    normCoord = coord;
  }
  auto fragBuilder = args.fragBuilder;
  fragBuilder->codeAppendf("vec4 %s = ", out);
  fragBuilder->appendTextureLookup((*args.textureSamplers)[0], normCoord);
  fragBuilder->codeAppend(";");
}

void GLSLTiledTextureEffect::subsetCoord(EmitArgs& args, TiledTextureEffect::ShaderMode mode,
                                         const std::string& subsetName, const char* coordSwizzle,
                                         const char* subsetStartSwizzle,
                                         const char* subsetStopSwizzle, const char* extraCoord,
                                         const char* coordWeight) const {
  auto fragBuilder = args.fragBuilder;
  switch (mode) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
    case TiledTextureEffect::ShaderMode::Clamp:
      fragBuilder->codeAppendf("subsetCoord.%s = inCoord.%s;", coordSwizzle, coordSwizzle);
      break;
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
      fragBuilder->codeAppendf("subsetCoord.%s = mod(inCoord.%s - %s.%s, %s.%s - %s.%s) + %s.%s;",
                               coordSwizzle, coordSwizzle, subsetName.c_str(), subsetStartSwizzle,
                               subsetName.c_str(), subsetStopSwizzle, subsetName.c_str(),
                               subsetStartSwizzle, subsetName.c_str(), subsetStartSwizzle);
      break;
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
      fragBuilder->codeAppend("{");
      fragBuilder->codeAppendf("float w = %s.%s - %s.%s;", subsetName.c_str(), subsetStopSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppendf("float w2 = 2.0 * w;");
      fragBuilder->codeAppendf("float d = inCoord.%s - %s.%s;", coordSwizzle, subsetName.c_str(),
                               subsetStartSwizzle);
      fragBuilder->codeAppend("float m = mod(d, w2);");
      fragBuilder->codeAppend("float o = mix(m, w2 - m, step(w, m));");
      fragBuilder->codeAppendf("subsetCoord.%s = o + %s.%s;", coordSwizzle, subsetName.c_str(),
                               subsetStartSwizzle);
      fragBuilder->codeAppendf("%s = w - o + %s.%s;", extraCoord, subsetName.c_str(),
                               subsetStartSwizzle);
      // coordWeight is used as the third param of mix() to blend between a sample taken using
      // subsetCoord and a sample at extraCoord.
      fragBuilder->codeAppend("float hw = w / 2.0;");
      fragBuilder->codeAppend("float n = mod(d - hw, w2);");
      fragBuilder->codeAppendf("%s = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);",
                               coordWeight);
      fragBuilder->codeAppend("}");
      break;
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
      fragBuilder->codeAppend("{");
      fragBuilder->codeAppendf("float w = %s.%s - %s.%s;", subsetName.c_str(), subsetStopSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppendf("float w2 = 2.0 * w;");
      fragBuilder->codeAppendf("float m = mod(inCoord.%s - %s.%s, w2);", coordSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppendf("subsetCoord.%s = mix(m, w2 - m, step(w, m)) + %s.%s;", coordSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppend("}");
      break;
  }
}

void GLSLTiledTextureEffect::clampCoord(EmitArgs& args, bool clamp, const std::string& clampName,
                                        const char* coordSwizzle, const char* clampStartSwizzle,
                                        const char* clampStopSwizzle) const {
  auto fragBuilder = args.fragBuilder;
  if (clamp) {
    fragBuilder->codeAppendf("clampedCoord%s = clamp(subsetCoord%s, %s%s, %s%s);", coordSwizzle,
                             coordSwizzle, clampName.c_str(), clampStartSwizzle, clampName.c_str(),
                             clampStopSwizzle);
  } else {
    fragBuilder->codeAppendf("clampedCoord%s = subsetCoord%s;", coordSwizzle, coordSwizzle);
  }
}

void GLSLTiledTextureEffect::clampCoord(EmitArgs& args, const bool useClamp[2],
                                        const std::string& clampName) const {
  if (useClamp[0] == useClamp[1]) {
    clampCoord(args, useClamp[0], clampName, "", ".xy", ".zw");
  } else {
    clampCoord(args, useClamp[0], clampName, ".x", ".x", ".z");
    clampCoord(args, useClamp[1], clampName, ".y", ".y", ".w");
  }
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
  bool sampleCoordsMustBeNormalized =
      textureView->getTexture()->type() != GPUTextureType::Rectangle;
  if (unormCoordsRequiredForShaderMode && sampleCoordsMustBeNormalized) {
    names.dimensionsName =
        uniformHandler->addUniform("Dimension", UniformFormat::Float2, ShaderStage::Fragment);
  }
  return names;
}

void GLSLTiledTextureEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    // emit a transparent color as the output color.
    fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
    return;
  }
  auto vertexColor = (*args.transformedCoords)[0].name();
  if (args.coordFunc) {
    vertexColor = args.coordFunc(vertexColor);
  }
  Sampling sampling(textureView, samplerState, subset);
  if (sampling.shaderModeX == TiledTextureEffect::ShaderMode::None &&
      sampling.shaderModeY == TiledTextureEffect::ShaderMode::None) {
    fragBuilder->codeAppendf("%s = ", args.outputColor.c_str());
    fragBuilder->appendTextureLookup((*args.textureSamplers)[0], vertexColor);
    fragBuilder->codeAppend(";");
  } else {
    fragBuilder->codeAppendf("vec2 inCoord = %s;", vertexColor.c_str());
    bool useClamp[2] = {ShaderModeUsesClamp(sampling.shaderModeX),
                        ShaderModeUsesClamp(sampling.shaderModeY)};
    auto names = initUniform(args, textureView, sampling, useClamp);
    if (!names.dimensionsName.empty()) {
      fragBuilder->codeAppendf("inCoord /= %s;", names.dimensionsName.c_str());
    }

    const char* extraRepeatCoordX = nullptr;
    const char* repeatCoordWeightX = nullptr;
    const char* extraRepeatCoordY = nullptr;
    const char* repeatCoordWeightY = nullptr;

    bool mipmapRepeatX =
        sampling.shaderModeX == TiledTextureEffect::ShaderMode::RepeatNearestMipmap ||
        sampling.shaderModeX == TiledTextureEffect::ShaderMode::RepeatLinearMipmap;
    bool mipmapRepeatY =
        sampling.shaderModeY == TiledTextureEffect::ShaderMode::RepeatNearestMipmap ||
        sampling.shaderModeY == TiledTextureEffect::ShaderMode::RepeatLinearMipmap;

    if (mipmapRepeatX || mipmapRepeatY) {
      fragBuilder->codeAppend("vec2 extraRepeatCoord;");
    }
    if (mipmapRepeatX) {
      fragBuilder->codeAppend("float repeatCoordWeightX;");
      extraRepeatCoordX = "extraRepeatCoord.x";
      repeatCoordWeightX = "repeatCoordWeightX";
    }
    if (mipmapRepeatY) {
      fragBuilder->codeAppend("float repeatCoordWeightY;");
      extraRepeatCoordY = "extraRepeatCoord.y";
      repeatCoordWeightY = "repeatCoordWeightY";
    }

    fragBuilder->codeAppend("highp vec2 subsetCoord;");
    subsetCoord(args, sampling.shaderModeX, names.subsetName, "x", "x", "z", extraRepeatCoordX,
                repeatCoordWeightX);
    subsetCoord(args, sampling.shaderModeY, names.subsetName, "y", "y", "w", extraRepeatCoordY,
                repeatCoordWeightY);

    fragBuilder->codeAppend("highp vec2 clampedCoord;");
    clampCoord(args, useClamp, names.clampName);

    if (constraint == SrcRectConstraint::Strict) {
      std::string subsetName = args.inputSubset;
      if (!names.dimensionsName.empty()) {
        fragBuilder->codeAppendf("highp vec4 extraSubset = %s;", subsetName.c_str());
        subsetName = "extraSubset";
        fragBuilder->codeAppendf("extraSubset.xy /= %s;", names.dimensionsName.c_str());
        fragBuilder->codeAppendf("extraSubset.zw /= %s;", names.dimensionsName.c_str());
      }
      args.fragBuilder->codeAppendf("clampedCoord = clamp(clampedCoord, %s.xy, %s.zw);",
                                    subsetName.c_str(), subsetName.c_str());
    }

    if (mipmapRepeatX && mipmapRepeatY) {
      fragBuilder->codeAppendf("extraRepeatCoord = clamp(extraRepeatCoord, %s.xy, %s.zw);",
                               names.clampName.c_str(), names.clampName.c_str());
    } else if (mipmapRepeatX) {
      fragBuilder->codeAppendf("extraRepeatCoord.x = clamp(extraRepeatCoord.x, %s.x, %s.z);",
                               names.clampName.c_str(), names.clampName.c_str());
    } else if (mipmapRepeatY) {
      fragBuilder->codeAppendf("extraRepeatCoord.y = clamp(extraRepeatCoord.y, %s.y, %s.w);",
                               names.clampName.c_str(), names.clampName.c_str());
    }

    if (mipmapRepeatX && mipmapRepeatY) {
      const char* textureColor1 = "textureColor1";
      readColor(args, names.dimensionsName, "clampedCoord", textureColor1);
      const char* textureColor2 = "textureColor2";
      readColor(args, names.dimensionsName, "vec2(extraRepeatCoord.x, clampedCoord.y)",
                textureColor2);
      const char* textureColor3 = "textureColor3";
      readColor(args, names.dimensionsName, "vec2(clampedCoord.x, extraRepeatCoord.y)",
                textureColor3);
      const char* textureColor4 = "textureColor4";
      readColor(args, names.dimensionsName, "vec2(extraRepeatCoord.x, extraRepeatCoord.y)",
                textureColor4);
      fragBuilder->codeAppendf(
          "vec4 textureColor = mix(mix(%s, %s, repeatCoordWeightX), mix(%s, %s, "
          "repeatCoordWeightX), repeatCoordWeightY);",
          textureColor1, textureColor2, textureColor3, textureColor4);
    } else if (mipmapRepeatX) {
      const char* textureColor1 = "textureColor1";
      readColor(args, names.dimensionsName, "clampedCoord", textureColor1);
      const char* textureColor2 = "textureColor2";
      readColor(args, names.dimensionsName, "vec2(extraRepeatCoord.x, clampedCoord.y)",
                textureColor2);
      fragBuilder->codeAppendf("vec4 textureColor = mix(%s, %s, repeatCoordWeightX);",
                               textureColor1, textureColor2);
    } else if (mipmapRepeatY) {
      const char* textureColor1 = "textureColor1";
      readColor(args, names.dimensionsName, "clampedCoord", textureColor1);
      const char* textureColor2 = "textureColor2";
      readColor(args, names.dimensionsName, "vec2(clampedCoord.x, extraRepeatCoord.y)",
                textureColor2);
      fragBuilder->codeAppendf("vec4 textureColor = mix(%s, %s, repeatCoordWeightY);",
                               textureColor1, textureColor2);
    } else {
      readColor(args, names.dimensionsName, "clampedCoord", "textureColor");
    }

    static const char* repeatReadX = "repeatReadX";
    static const char* repeatReadY = "repeatReadY";
    bool repeatX = sampling.shaderModeX == TiledTextureEffect::ShaderMode::RepeatLinearNone ||
                   sampling.shaderModeX == TiledTextureEffect::ShaderMode::RepeatLinearMipmap;
    bool repeatY = sampling.shaderModeY == TiledTextureEffect::ShaderMode::RepeatLinearNone ||
                   sampling.shaderModeY == TiledTextureEffect::ShaderMode::RepeatLinearMipmap;
    if (repeatX || sampling.shaderModeX == TiledTextureEffect::ShaderMode::ClampToBorderLinear) {
      fragBuilder->codeAppend("float errX = subsetCoord.x - clampedCoord.x;");
      if (repeatX) {
        fragBuilder->codeAppendf("float repeatCoordX = errX > 0.0 ? %s.x : %s.z;",
                                 names.clampName.c_str(), names.clampName.c_str());
      }
    }
    if (repeatY || sampling.shaderModeY == TiledTextureEffect::ShaderMode::ClampToBorderLinear) {
      fragBuilder->codeAppend("float errY = subsetCoord.y - clampedCoord.y;");
      if (repeatY) {
        fragBuilder->codeAppendf("float repeatCoordY = errY > 0.0 ? %s.y : %s.w;",
                                 names.clampName.c_str(), names.clampName.c_str());
      }
    }

    const char* ifStr = "if";
    if (repeatX && repeatY) {
      readColor(args, names.dimensionsName, "vec2(repeatCoordX, clampedCoord.y)", repeatReadX);
      readColor(args, names.dimensionsName, "vec2(clampedCoord.x, repeatCoordY)", repeatReadY);
      static const char* repeatReadXY = "repeatReadXY";
      readColor(args, names.dimensionsName, "vec2(repeatCoordX, repeatCoordY)", repeatReadXY);
      fragBuilder->codeAppend("if (errX != 0.0 && errY != 0.0) {");
      fragBuilder->codeAppend("errX = abs(errX);");
      fragBuilder->codeAppendf(
          "textureColor = mix(mix(textureColor, %s, errX), mix(%s, %s, errX), abs(errY));",
          repeatReadX, repeatReadY, repeatReadXY);
      fragBuilder->codeAppend("}");
      ifStr = "else if";
    }
    if (repeatX) {
      fragBuilder->codeAppendf("%s (errX != 0.0) {", ifStr);
      readColor(args, names.dimensionsName, "vec2(repeatCoordX, clampedCoord.y)", repeatReadX);
      fragBuilder->codeAppendf("textureColor = mix(textureColor, %s, errX);", repeatReadX);
      fragBuilder->codeAppend("}");
    }
    if (repeatY) {
      fragBuilder->codeAppendf("%s (errY != 0.0) {", ifStr);
      readColor(args, names.dimensionsName, "vec2(clampedCoord.x, repeatCoordY)", repeatReadY);
      fragBuilder->codeAppendf("textureColor = mix(textureColor, %s, errY);", repeatReadY);
      fragBuilder->codeAppend("}");
    }

    if (sampling.shaderModeX == TiledTextureEffect::ShaderMode::ClampToBorderLinear) {
      fragBuilder->codeAppend("textureColor = mix(textureColor, vec4(0.0), min(abs(errX), 1.0));");
    }
    if (sampling.shaderModeY == TiledTextureEffect::ShaderMode::ClampToBorderLinear) {
      fragBuilder->codeAppendf("textureColor = mix(textureColor, vec4(0.0), min(abs(errY), 1.0));");
    }
    if (sampling.shaderModeX == TiledTextureEffect::ShaderMode::ClampToBorderNearest) {
      fragBuilder->codeAppend("float snappedX = floor(inCoord.x + 0.001) + 0.5;");
      fragBuilder->codeAppendf("if (snappedX < %s.x || snappedX > %s.z) {",
                               names.subsetName.c_str(), names.subsetName.c_str());
      fragBuilder->codeAppend("textureColor = vec4(0.0);");  // border color
      fragBuilder->codeAppend("}");
    }
    if (sampling.shaderModeY == TiledTextureEffect::ShaderMode::ClampToBorderNearest) {
      fragBuilder->codeAppend("float snappedY = floor(inCoord.y + 0.001) + 0.5;");
      fragBuilder->codeAppendf("if (snappedY < %s.y || snappedY > %s.w) {",
                               names.subsetName.c_str(), names.subsetName.c_str());
      fragBuilder->codeAppend("textureColor = vec4(0.0);");  // border color
      fragBuilder->codeAppend("}");
    }
    fragBuilder->codeAppendf("%s = textureColor;", args.outputColor.c_str());
  }
  if (textureProxy->isAlphaOnly()) {
    args.fragBuilder->codeAppendf("%s = %s.a * %s;", args.outputColor.c_str(),
                                  args.outputColor.c_str(), args.inputColor.c_str());
  } else {
    args.fragBuilder->codeAppendf("%s = %s * %s.a;", args.outputColor.c_str(),
                                  args.outputColor.c_str(), args.inputColor.c_str());
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
                             textureView->getTexture()->type() != GPUTextureType::Rectangle;
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
    if (!hasDimensionUniform && type != GPUTextureType::Rectangle) {
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
