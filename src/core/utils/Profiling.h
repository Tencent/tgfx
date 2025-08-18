/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
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
#pragma once
#ifdef TGFX_USE_INSPECTOR
#include <optional>
#include "Define.h"
#include "gpu/Pipeline.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
class TGFXTypeToInspector {
 public:
  static void SendAttributeData(const char* name, const tgfx::Rect& rect) {
    float value[4] = {rect.left, rect.right, rect.top, rect.bottom};
    inspector::Inspector::SendAttributeData(name, value, 4);
  }

  static void SendAttributeData(const char* name, const std::optional<tgfx::Matrix>& matrix) {
    auto value = Matrix::MakeAll(1, 0, 0, 0, 1, 0);
    if (matrix.has_value()) {
      value = matrix.value();
    }
    SendAttributeData(name, value);
  }

  static void SendAttributeData(const char* name, const tgfx::Matrix& matrix) {
    float value[6] = {1, 0, 0, 0, 1, 0};
    value[0] = matrix.getScaleX();
    value[1] = matrix.getSkewX();
    value[2] = matrix.getTranslateX();
    value[3] = matrix.getSkewY();
    value[4] = matrix.getScaleY();
    value[5] = matrix.getTranslateY();
    inspector::Inspector::SendAttributeData(name, value, 6);
  }

  static void SendAttributeData(const char* name, const std::optional<tgfx::Color>& color) {
    auto value = Color::FromRGBA(255, 255, 255, 255);
    if (color.has_value()) {
      value = color.value();
    }
    SendAttributeData(name, value);
  }

  static void SendAttributeData(const char* name, const tgfx::Color& color) {
    auto r = static_cast<uint8_t>(color.red * 255.f);
    auto g = static_cast<uint8_t>(color.green * 255.f);
    auto b = static_cast<uint8_t>(color.blue * 255.f);
    auto a = static_cast<uint8_t>(color.alpha * 255.f);
    auto value = static_cast<uint32_t>(r | g << 8 | b << 16 | a << 24);
    inspector::Inspector::SendAttributeData(name, value, inspector::MsgType::ValueDataColor);
  }

  static void SendPipelineData(const PlacementPtr<Pipeline>& pipeline) {
    for (size_t i = 0; i < pipeline->numFragmentProcessors(); ++i) {
      auto processor = pipeline->getFragmentProcessor(i);
      FragmentProcessor::Iter fpIter(processor);
      while (const auto* subFP = fpIter.next()) {
        for (size_t j = 0; j < subFP->numTextureSamplers(); ++j) {
          const auto* sampler = subFP->textureSampler(i);
          OperateTextureSampler(reinterpret_cast<uint64_t>(sampler));
        }
      }
    }
  }

  static void SendTextureData(TextureSampler* samplerPtr, int width, int height, size_t rowBytes,
                              PixelFormat format, const void* pixels) {
    inspector::Inspector::SendTextureData(reinterpret_cast<uint64_t>(samplerPtr), width, height,
                                          rowBytes, static_cast<uint8_t>(format), pixels);
  }
};

#define AttributeTGFXName(name, value) TGFXTypeToInspector::SendAttributeData(name, value)
#define TextureData(samplerPtr, width, height, rowBytes, format, pixels) \
  TGFXTypeToInspector::SendTextureData(samplerPtr, width, height, rowBytes, format, pixels)
#define OperatePiplineData(pipline) TGFXTypeToInspector::SendPipelineData(pipline)
}  // namespace tgfx
#else
#define FrameMark

#define ScopedMark(type, active)
#define OperateMark(type)
#define TaskMark(type)

#define AttributeName(name, value)
#define AttributeTGFXName(name, value)
#define AttributeNameFloatArray(name, value, size)
#define AttributeNameEnum(name, value, type)
#define AttributeEnum(value, type)

#define TextureData(samplerPtr, width, height, rowBytes, format, pixels)
#define OperatePiplineData(pipline)
#define OperateTextureSampler(sampler)

#define SEND_LAYER_DATA(data)
#define LAYER_CALLBACK(x)
#endif