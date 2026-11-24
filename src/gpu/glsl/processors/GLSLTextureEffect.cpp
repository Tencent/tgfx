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

#include "GLSLTextureEffect.h"
#include <unordered_map>
#include "core/utils/Log.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

namespace tgfx {
static std::array<float, 12> AlignMat3(const float* mat3) {
  // clang-format off
  return {
    mat3[0], mat3[1], mat3[2], 0.0f,
    mat3[3], mat3[4], mat3[5], 0.0f,
    mat3[6], mat3[7], mat3[8], 0.0f
  };
  // clang-format on
}

static constexpr float ColorConversion601LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.391762f, 2.017232f, 1.596027f, -0.812968f, 0.0f,
};

static constexpr float ColorConversion601FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772f, 1.402f, -0.714136f, 0.0f,
};

static constexpr float ColorConversion709LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.213249f, 2.112402f, 1.792741f, -0.532909f, 0.0f,
};

static constexpr float ColorConversion709FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.187324f, 1.8556f, 1.5748f, -0.468124f, 0.0f,
};

static constexpr float ColorConversion2020LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.187326f, 2.141772f, 1.678674f, -0.650424f, 0.0f,
};

static constexpr float ColorConversion2020FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.164553f, 1.8814f, 1.4746f, -0.571353f, 0.0f,
};

static constexpr float ColorConversionJPEGFullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772000f, 1.402f, -0.714136f, 0.0f,
};

PlacementPtr<FragmentProcessor> TextureEffect::MakeRGBAAA(BlockAllocator* allocator,
                                                          std::shared_ptr<TextureProxy> proxy,
                                                          const SamplingArgs& args,
                                                          const Point& alphaStart,
                                                          const Matrix* uvMatrix) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto matrix = uvMatrix ? *uvMatrix : Matrix::I();
  return allocator->make<GLSLTextureEffect>(std::move(proxy), alphaStart, args.sampling,
                                            args.constraint, matrix, args.sampleArea);
}

GLSLTextureEffect::GLSLTextureEffect(std::shared_ptr<TextureProxy> proxy, const Point& alphaStart,
                                     const SamplingOptions& sampling, SrcRectConstraint constraint,
                                     const Matrix& uvMatrix, const std::optional<Rect>& subset)
    : TextureEffect(std::move(proxy), sampling, constraint, alphaStart, uvMatrix, subset) {
}

void GLSLTextureEffect::emitCode(EmitArgs& args) const {
  auto textureView = getTextureView();
  auto fragBuilder = args.fragBuilder;
  if (textureView == nullptr) {
    // emit a transparent color as the output color.
    fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
    return;
  }
  if (textureView->isYUV()) {
    emitYUVTextureCode(args);
  } else {
    emitDefaultTextureCode(args);
  }
  auto entry = fragBuilder->appendFunction("tgfxMulAlpha");
  if (entry.empty()) {
    return;
  }
  if (textureProxy->isAlphaOnly()) {
    fragBuilder->appendCall(args.outputColor, entry, {args.inputColor, args.outputColor});
  } else {
    fragBuilder->appendCall(args.outputColor, entry, {args.outputColor, args.inputColor});
  }
}

void GLSLTextureEffect::emitDefaultTextureCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  if (args.textureSamplers == nullptr || args.textureSamplers->count() == 0) {
    // The sampler the processor declares was not registered by the install walk: the tree and
    // the registration disagree, so emit a transparent pixel and report the offending processor
    // instead of dereferencing a missing sampler handle.
    LOGE("GLSLTextureEffect: no sampler registered for %s (view=%p); the draw renders transparent",
         name().c_str(), static_cast<const void*>(getTextureView()));
    fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
    return;
  }
  auto& textureSampler = (*args.textureSamplers)[0];
  auto texCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  if (args.coordFunc) {
    texCoordName = args.coordFunc(texCoordName);
  }
  std::string subsetName = "";
  if (needSubset()) {
    subsetName = uniformHandler->addUniform("Subset", UniformFormat::Float4, ShaderStage::Fragment);
  }
  std::string extraSubsetName = "";
  if (SrcRectConstraint::Strict == constraint) {
    extraSubsetName = args.inputSubset;
  }
  ValueID strictSubsetValue = INVALID_VALUE_ID;
  ValueID drawSubsetValue = INVALID_VALUE_ID;
  if (args.irBuilder != nullptr) {
    if (!extraSubsetName.empty()) {
      strictSubsetValue = args.irBuilder->InternValue(extraSubsetName, SLType::Float4,
                                                      Precision::High, IRValueKind::Uniform);
    }
    if (!subsetName.empty()) {
      drawSubsetValue = args.irBuilder->InternValue(subsetName, SLType::Float4, Precision::High,
                                                    IRValueKind::Uniform);
    }
  }
  // The read keeps the clamp the call site already composed: a clamped coordinate is a local the
  // plan cannot name, so those draws stay on their fallback.
  if (alphaStart == Point::Zero()) {
    bool recorded = false;
    const auto read = fragBuilder->readTexture(
        textureSampler, texCoordName, extraSubsetName, subsetName, &recorded, args.irBuilder,
        args.samplerValue(textureSampler), (*args.transformedCoords)[0].type(), strictSubsetValue,
        drawSubsetValue);
    fragBuilder->storeColor(args.outputColor, read.name);
    if (args.irBuilder != nullptr && args.outputValue != INVALID_VALUE_ID &&
        read.value != INVALID_VALUE_ID) {
      args.irBuilder->StoreOutput(args.outputValue, read.value);
    }
    return;
  }
  const auto finalCoordName =
      fragBuilder->clampToSubsets(texCoordName, extraSubsetName, subsetName);
  bool recorded = false;
  const auto read = fragBuilder->readTexture(textureSampler, finalCoordName, "", "", &recorded,
                                             args.irBuilder, args.samplerValue(textureSampler),
                                             (*args.transformedCoords)[0].type());
  fragBuilder->aliasValue("color", read.name, "vec4");
  if (alphaStart != Point::Zero()) {
    if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID ||
        read.value == INVALID_VALUE_ID) {
      return;
    }
    auto alphaStartName =
        uniformHandler->addUniform("AlphaStart", UniformFormat::Float2, ShaderStage::Fragment);
    const auto alphaSampler = args.samplerValue(textureSampler);
    const auto alphaStartValue = args.irBuilder->InternValue(alphaStartName, SLType::Float2,
                                                             Precision::High, IRValueKind::Uniform);
    const auto finalCoordValue = args.irBuilder->InternValue(finalCoordName, SLType::Float2,
                                                             Precision::High, IRValueKind::Local);
    const auto coordValue =
        finalCoordValue != INVALID_VALUE_ID && alphaStartValue != INVALID_VALUE_ID
            ? args.irBuilder->CreateBinary(IRBinaryOp::Add, SLType::Float2, finalCoordValue,
                                           alphaStartValue, "alphaVertexColor")
            : INVALID_VALUE_ID;
    const auto colorValue =
        args.irBuilder->InternValue("color", SLType::Float4, Precision::High, IRValueKind::Local);
    if (alphaSampler == INVALID_VALUE_ID || coordValue == INVALID_VALUE_ID) {
      args.irBuilder->MarkFailed("the alpha texture coordinate could not be recorded");
      return;
    }
    args.irBuilder->Assign({colorValue, ""}, read.value);
    const auto alphaValue =
        args.irBuilder->InternValue("alpha", SLType::Float4, Precision::High, IRValueKind::Local);
    const auto zero = args.irBuilder->CreateConstant(0.0f);
    const auto one = args.irBuilder->CreateConstant(1.0f);
    const auto clampedColor =
        args.irBuilder->CreateCall(OperatorID::Clamp, SLType::Float4, {colorValue, zero, one});
    const auto alphaSample = args.irBuilder->CreateTextureSample(
        alphaSampler, coordValue, SLType::Float4, "alphaRead", SLType::Texture2DSampler);
    args.irBuilder->Assign({alphaValue, ""}, alphaSample);
    const auto clampedAlpha =
        args.irBuilder->CreateCall(OperatorID::Clamp, SLType::Float4, {alphaValue, zero, one});
    args.irBuilder->Assign({colorValue, ""}, clampedColor);
    args.irBuilder->Assign({alphaValue, ""}, clampedAlpha);
    const auto alphaR = args.irBuilder->CreateSwizzle(alphaValue, SLType::Float, "r");
    const auto colorRgb = args.irBuilder->CreateSwizzle(colorValue, SLType::Float3, "rgb");
    const auto premultiplied =
        args.irBuilder->CreateBinary(IRBinaryOp::Mul, SLType::Float3, colorRgb, alphaR);
    const auto result = args.irBuilder->CreateConstruct(SLType::Float4, {premultiplied, alphaR});
    if (result == INVALID_VALUE_ID) {
      args.irBuilder->MarkFailed("the alpha modulation could not be recorded");
      return;
    }
    args.irBuilder->Assign({colorValue, ""}, result);
    args.irBuilder->StoreOutput(args.outputValue, result);

    std::string coordExpression = {};
    std::string colorExpression = {};
    std::string alphaExpression = {};
    std::string resultExpression = {};
    std::string error = {};
    if (!GLSLOperatorEmitter::EmitValue(args.irBuilder->IR(), coordValue, &coordExpression,
                                        &error) ||
        !GLSLOperatorEmitter::EmitValue(args.irBuilder->IR(), clampedColor, &colorExpression,
                                        &error) ||
        !GLSLOperatorEmitter::EmitValue(args.irBuilder->IR(), clampedAlpha, &alphaExpression,
                                        &error) ||
        !GLSLOperatorEmitter::EmitValue(args.irBuilder->IR(), result, &resultExpression, &error)) {
      args.irBuilder->MarkFailed(error);
      return;
    }
    fragBuilder->aliasValue("color", colorExpression, "");
    fragBuilder->aliasValue("alphaVertexColor", coordExpression, "highp vec2");
    fragBuilder->aliasValue(
        "alpha", fragBuilder->textureLookupExpression(textureSampler, "alphaVertexColor"), "vec4");
    fragBuilder->aliasValue("alpha", alphaExpression, "");
    fragBuilder->aliasValue("color", resultExpression, "");
  }
  fragBuilder->storeColor(args.outputColor, "color");
}

void GLSLTextureEffect::emitYUVTextureCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  auto yuvTexture = getYUVTexture();
  auto& textureSamplers = *args.textureSamplers;
  const auto texCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  std::string subsetName = "";
  if (needSubset()) {
    subsetName = uniformHandler->addUniform("Subset", UniformFormat::Float4, ShaderStage::Fragment);
  }
  std::string extraSubsetName = "";
  if (SrcRectConstraint::Strict == constraint) {
    extraSubsetName = args.inputSubset;
  }
  auto finalCoordName = fragBuilder->clampToSubsets(texCoordName, extraSubsetName, subsetName);
  auto recordSample = [&](size_t samplerIndex, const std::string& coordName,
                          const std::string& baseName, const std::string& swizzle,
                          const std::string& field, SLType type) {
    if (args.irBuilder == nullptr) {
      return;
    }
    const auto sampler = args.samplerValue(textureSamplers[samplerIndex]);
    const auto coordinate =
        args.irBuilder->InternValue(coordName, SLType::Float2, Precision::High, IRValueKind::Local);
    if (sampler == INVALID_VALUE_ID || coordinate == INVALID_VALUE_ID) {
      return;
    }
    const auto sample = args.irBuilder->CreateTextureSample(sampler, coordinate, SLType::Float4, {},
                                                            SLType::Texture2DSampler);
    const auto channel = args.irBuilder->CreateSwizzle(sample, type, swizzle);
    const auto base =
        args.irBuilder->InternValue(baseName, baseName == "yuv" ? SLType::Float3 : SLType::Float,
                                    Precision::High, IRValueKind::Local);
    args.irBuilder->Assign({base, field}, channel);
  };
  fragBuilder->codeAppend("vec3 yuv;");
  fragBuilder->aliasValue(
      "yuv.x", fragBuilder->textureLookupExpression(textureSamplers[0], finalCoordName) + ".r", "");
  recordSample(0, finalCoordName, "yuv", "r", "x", SLType::Float);
  if (yuvTexture->yuvFormat() == YUVFormat::I420) {
    auto chromaCoord = fragBuilder->clampToSubsets(texCoordName, extraSubsetName, subsetName);
    fragBuilder->aliasValue(
        "yuv.y", fragBuilder->textureLookupExpression(textureSamplers[1], chromaCoord) + ".r", "");
    recordSample(1, chromaCoord, "yuv", "r", "y", SLType::Float);
    chromaCoord = fragBuilder->clampToSubsets(texCoordName, extraSubsetName, subsetName);
    fragBuilder->aliasValue(
        "yuv.z", fragBuilder->textureLookupExpression(textureSamplers[2], chromaCoord) + ".r", "");
    recordSample(2, chromaCoord, "yuv", "r", "z", SLType::Float);
  } else if (yuvTexture->yuvFormat() == YUVFormat::NV12) {
    auto chromaCoord = fragBuilder->clampToSubsets(texCoordName, extraSubsetName, subsetName);
    fragBuilder->aliasValue(
        "yuv.yz", fragBuilder->textureLookupExpression(textureSamplers[1], chromaCoord) + ".ra",
        "");
    recordSample(1, chromaCoord, "yuv", "ra", "yz", SLType::Float2);
  }
  auto mat3Name = uniformHandler->addUniform("Mat3ColorConversion", UniformFormat::Float3x3,
                                             ShaderStage::Fragment);
  auto entry = fragBuilder->appendFunction("tgfxYUVToRGB");
  if (entry.empty()) {
    return;
  }
  const std::string limitedRange =
      std::to_string(IsLimitedYUVColorRange(yuvTexture->yuvColorSpace()) ? 1 : 0);
  auto emitYUVCall = [&](bool hasAlpha, std::string* expression) -> bool {
    if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID) {
      return false;
    }
    const auto yuvValue =
        args.irBuilder->InternValue("yuv", SLType::Float3, Precision::High, IRValueKind::Local);
    const auto matrixValue = args.irBuilder->InternValue(mat3Name, SLType::Float3x3,
                                                         Precision::High, IRValueKind::Uniform);
    const auto limitedRangeValue = args.irBuilder->CreateIntConstant(
        IsLimitedYUVColorRange(yuvTexture->yuvColorSpace()) ? 1 : 0);
    const auto hasAlphaValue = args.irBuilder->CreateIntConstant(hasAlpha ? 1 : 0);
    const auto alphaValue = hasAlpha
                                ? args.irBuilder->InternValue("yuv_a", SLType::Float,
                                                              Precision::High, IRValueKind::Local)
                                : args.irBuilder->CreateConstant(1.0f);
    const auto result = args.irBuilder->CreateCall(
        OperatorID::YUVToRGB, SLType::Float4,
        {yuvValue, matrixValue, limitedRangeValue, hasAlphaValue, alphaValue});
    if (result == INVALID_VALUE_ID) {
      args.irBuilder->MarkFailed("the YUV operator could not be recorded");
      return false;
    }
    args.irBuilder->StoreOutput(args.outputValue, result);
    std::string error = {};
    if (!GLSLOperatorEmitter::EmitCall(args.irBuilder->IR(), result, expression, &error)) {
      args.irBuilder->MarkFailed(error);
      return false;
    }
    return true;
  };
  if (alphaStart == Point::Zero()) {
    std::string expression = {};
    if (!emitYUVCall(false, &expression)) {
      return;
    }
    fragBuilder->appendCall(args.outputColor, entry, {"yuv", mat3Name, limitedRange, "0", "1.0"},
                            "", expression);
  } else {
    auto alphaStartName =
        uniformHandler->addUniform("AlphaStart", UniformFormat::Float2, ShaderStage::Fragment);
    std::string alphaVertexColor = "alphaVertexColor";
    fragBuilder->aliasValue("alphaVertexColor", finalCoordName + " + " + alphaStartName,
                            "highp vec2");
    fragBuilder->aliasValue(
        "yuv_a", fragBuilder->textureLookupExpression(textureSamplers[0], alphaVertexColor) + ".r",
        "float");
    recordSample(0, alphaVertexColor, "yuv_a", "r", "", SLType::Float);
    std::string expression = {};
    if (!emitYUVCall(true, &expression)) {
      return;
    }
    fragBuilder->appendCall(args.outputColor, entry, {"yuv", mat3Name, limitedRange, "1", "yuv_a"},
                            "", expression);
  }
}

void GLSLTextureEffect::onSetData(UniformData* /*vertexUniformData*/,
                                  UniformData* fragmentUniformData) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return;
  }
  if (alphaStart != Point::Zero()) {
    auto alphaStartValue = textureView->getTextureCoord(alphaStart.x, alphaStart.y);
    fragmentUniformData->setData("AlphaStart", alphaStartValue);
  }
  auto yuvTexture = getYUVTexture();
  if (yuvTexture) {
    std::string mat3ColorConversion = "Mat3ColorConversion";
    switch (yuvTexture->yuvColorSpace()) {
      case YUVColorSpace::BT601_LIMITED: {
        fragmentUniformData->setData(mat3ColorConversion, AlignMat3(ColorConversion601LimitRange));
      } break;
      case YUVColorSpace::BT601_FULL:
        fragmentUniformData->setData(mat3ColorConversion, AlignMat3(ColorConversion601FullRange));
        break;
      case YUVColorSpace::BT709_LIMITED:
        fragmentUniformData->setData(mat3ColorConversion, AlignMat3(ColorConversion709LimitRange));
        break;
      case YUVColorSpace::BT709_FULL:
        fragmentUniformData->setData(mat3ColorConversion, AlignMat3(ColorConversion709FullRange));
        break;
      case YUVColorSpace::BT2020_LIMITED:
        fragmentUniformData->setData(mat3ColorConversion, AlignMat3(ColorConversion2020LimitRange));
        break;
      case YUVColorSpace::BT2020_FULL:
        fragmentUniformData->setData(mat3ColorConversion, AlignMat3(ColorConversion2020FullRange));
        break;
      case YUVColorSpace::JPEG_FULL:
        fragmentUniformData->setData(mat3ColorConversion, AlignMat3(ColorConversionJPEGFullRange));
        break;
      default:
        break;
    }
  }
  if (needSubset()) {
    auto subsetRect = subset.value_or(Rect::MakeWH(textureProxy->width(), textureProxy->height()));
    if (samplerState.magFilterMode == samplerState.minFilterMode &&
        samplerState.magFilterMode == FilterMode::Nearest) {
      subsetRect.roundOut();
    }
    auto type = textureView->getTexture()->type();
    // https://cs.android.com/android/platform/superproject/+/master:frameworks/native/libs/nativedisplay/surfacetexture/SurfaceTexture.cpp;l=275;drc=master;bpv=0;bpt=1
    // https://stackoverflow.com/questions/6023400/opengl-es-texture-coordinates-slightly-off
    // Normally this would just need to take 1/2 a texel off each end, but because the chroma
    // channels of YUV420 images are subsampled we may need to shrink the crop region by a whole
    // texel on each side.
    auto inset = type == TextureType::External ? 1.0f : 0.5f;
    subsetRect = subsetRect.makeInset(inset, inset);
    float rect[4] = {subsetRect.left, subsetRect.top, subsetRect.right, subsetRect.bottom};
    if (textureView->origin() == ImageOrigin::BottomLeft) {
      auto h = static_cast<float>(textureView->height());
      rect[1] = h - rect[1];
      rect[3] = h - rect[3];
      std::swap(rect[1], rect[3]);
    }
    if (type != TextureType::Rectangle) {
      auto lt = textureView->getTextureCoord(rect[0], rect[1]);
      auto rb = textureView->getTextureCoord(rect[2], rect[3]);
      rect[0] = lt.x;
      rect[1] = lt.y;
      rect[2] = rb.x;
      rect[3] = rb.y;
    }
    fragmentUniformData->setData("Subset", rect);
  }
}
}  // namespace tgfx
