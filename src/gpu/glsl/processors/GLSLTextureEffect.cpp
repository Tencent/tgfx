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
