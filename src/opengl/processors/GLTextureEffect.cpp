/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLTextureEffect.h"
#include <unordered_map>

namespace tgfx {
static const float ColorConversion601LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.391762f, 2.017232f, 1.596027f, -0.812968f, 0.0f,
};

static const float ColorConversion601FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772f, 1.402f, -0.714136f, 0.0f,
};

static const float ColorConversion709LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.213249f, 2.112402f, 1.792741f, -0.532909f, 0.0f,
};

static const float ColorConversion709FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.187324f, 1.8556f, 1.5748f, -0.468124f, 0.0f,
};

static const float ColorConversion2020LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.187326f, 2.141772f, 1.678674f, -0.650424f, 0.0f,
};

static const float ColorConversion2020FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.164553f, 1.8814f, 1.4746f, -0.571353f, 0.0f,
};

static const float ColorConversionJPEGFullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772000f, 1.402f, -0.714136f, 0.0f,
};

std::unique_ptr<FragmentProcessor> TextureEffect::MakeRGBAAA(std::shared_ptr<TextureProxy> proxy,
                                                             const Point& alphaStart,
                                                             const SamplingOptions& sampling,
                                                             const Matrix* localMatrix) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto matrix = localMatrix ? *localMatrix : Matrix::I();
  return std::make_unique<GLTextureEffect>(std::move(proxy), alphaStart, sampling, matrix);
}

GLTextureEffect::GLTextureEffect(std::shared_ptr<TextureProxy> proxy, const Point& alphaStart,
                                 SamplingOptions sampling, const Matrix& localMatrix)
    : TextureEffect(std::move(proxy), sampling, alphaStart, localMatrix) {
}

void GLTextureEffect::emitCode(EmitArgs& args) const {
  auto texture = getTexture();
  auto fragBuilder = args.fragBuilder;
  if (texture == nullptr) {
    // emit a transparent color as the output color.
    fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
    return;
  }
  if (texture->isYUV()) {
    emitYUVTextureCode(args);
  } else {
    emitPlainTextureCode(args);
  }
  if (textureProxy->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = %s.a * %s;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  } else {
    fragBuilder->codeAppendf("%s = %s * %s.a;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  }
}
void GLTextureEffect::emitPlainTextureCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto* uniformHandler = args.uniformHandler;
  auto& textureSampler = (*args.textureSamplers)[0];
  auto vertexColor = (*args.transformedCoords)[0].name();
  if (args.coordFunc) {
    vertexColor = args.coordFunc(vertexColor);
  }
  fragBuilder->codeAppend("vec4 color = ");
  fragBuilder->appendTextureLookup(textureSampler, vertexColor);
  fragBuilder->codeAppend(";");
  if (alphaStart != Point::Zero()) {
    fragBuilder->codeAppend("color = clamp(color, 0.0, 1.0);");
    auto alphaStartName =
        uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "AlphaStart");
    std::string alphaVertexColor = "alphaVertexColor";
    fragBuilder->codeAppendf("vec2 %s = %s + %s;", alphaVertexColor.c_str(), vertexColor.c_str(),
                             alphaStartName.c_str());
    fragBuilder->codeAppend("vec4 alpha = ");
    fragBuilder->appendTextureLookup(textureSampler, alphaVertexColor);
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("alpha = clamp(alpha, 0.0, 1.0);");
    fragBuilder->codeAppend("color = vec4(color.rgb * alpha.r, alpha.r);");
  }
  fragBuilder->codeAppendf("%s = color;", args.outputColor.c_str());
}

void GLTextureEffect::emitYUVTextureCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto* uniformHandler = args.uniformHandler;
  auto yuvTexture = getYUVTexture();
  auto& textureSamplers = *args.textureSamplers;
  auto vertexColor = (*args.transformedCoords)[0].name();
  fragBuilder->codeAppend("vec3 yuv;");
  fragBuilder->codeAppend("yuv.x = ");
  fragBuilder->appendTextureLookup(textureSamplers[0], vertexColor);
  fragBuilder->codeAppend(".r;");
  if (yuvTexture->pixelFormat() == YUVPixelFormat::I420) {
    fragBuilder->codeAppend("yuv.y = ");
    fragBuilder->appendTextureLookup(textureSamplers[1], vertexColor);
    fragBuilder->codeAppend(".r;");
    fragBuilder->codeAppend("yuv.z = ");
    fragBuilder->appendTextureLookup(textureSamplers[2], vertexColor);
    fragBuilder->codeAppend(".r;");
  } else if (yuvTexture->pixelFormat() == YUVPixelFormat::NV12) {
    fragBuilder->codeAppend("yuv.yz = ");
    fragBuilder->appendTextureLookup(textureSamplers[1], vertexColor);
    fragBuilder->codeAppend(".ra;");
  }
  if (IsLimitedYUVColorRange(yuvTexture->colorSpace())) {
    fragBuilder->codeAppend("yuv.x -= (16.0 / 255.0);");
  }
  fragBuilder->codeAppend("yuv.yz -= vec2(0.5, 0.5);");
  auto mat3Name =
      uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float3x3, "Mat3ColorConversion");
  fragBuilder->codeAppendf("vec3 rgb = clamp(%s * yuv, 0.0, 1.0);", mat3Name.c_str());
  if (alphaStart == Point::Zero()) {
    fragBuilder->codeAppendf("%s = vec4(rgb, 1.0);", args.outputColor.c_str());
  } else {
    auto alphaStartName =
        uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "AlphaStart");
    std::string alphaVertexColor = "alphaVertexColor";
    fragBuilder->codeAppendf("vec2 %s = %s + %s;", alphaVertexColor.c_str(), vertexColor.c_str(),
                             alphaStartName.c_str());
    fragBuilder->codeAppend("float yuv_a = ");
    fragBuilder->appendTextureLookup(textureSamplers[0], alphaVertexColor);
    fragBuilder->codeAppend(".r;");
    fragBuilder->codeAppend("yuv_a = (yuv_a - 16.0/255.0) / (219.0/255.0 - 1.0/255.0);");
    fragBuilder->codeAppend("yuv_a = clamp(yuv_a, 0.0, 1.0);");
    fragBuilder->codeAppendf("%s = vec4(rgb * yuv_a, yuv_a);", args.outputColor.c_str());
  }
}

void GLTextureEffect::onSetData(UniformBuffer* uniformBuffer) const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return;
  }
  if (alphaStart != Point::Zero()) {
    auto alphaStartValue = texture->getTextureCoord(alphaStart.x, alphaStart.y);
    uniformBuffer->setData("AlphaStart", alphaStartValue);
  }
  auto yuvTexture = getYUVTexture();
  if (yuvTexture) {
    std::string mat3ColorConversion = "Mat3ColorConversion";
    switch (yuvTexture->colorSpace()) {
      case YUVColorSpace::BT601_LIMITED:
        uniformBuffer->setData(mat3ColorConversion, ColorConversion601LimitRange);
        break;
      case YUVColorSpace::BT601_FULL:
        uniformBuffer->setData(mat3ColorConversion, ColorConversion601FullRange);
        break;
      case YUVColorSpace::BT709_LIMITED:
        uniformBuffer->setData(mat3ColorConversion, ColorConversion709LimitRange);
        break;
      case YUVColorSpace::BT709_FULL:
        uniformBuffer->setData(mat3ColorConversion, ColorConversion709FullRange);
        break;
      case YUVColorSpace::BT2020_LIMITED:
        uniformBuffer->setData(mat3ColorConversion, ColorConversion2020LimitRange);
        break;
      case YUVColorSpace::BT2020_FULL:
        uniformBuffer->setData(mat3ColorConversion, ColorConversion2020FullRange);
        break;
      case YUVColorSpace::JPEG_FULL:
        uniformBuffer->setData(mat3ColorConversion, ColorConversionJPEGFullRange);
        break;
      default:
        break;
    }
  }
}
}  // namespace tgfx
