/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <core/utils/Types.h>
#include <flatbuffers/flexbuffers.h>
#include <tgfx/core/GradientType.h>
#include <tgfx/layers/Layer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/TextAlign.h>
#include <string>

namespace tgfx {
class SerializeUtils {
 public:
  static std::string LayerTypeToString(LayerType type);

  static std::string BlendModeToString(BlendMode mode);

  static std::string StrokeAlignToString(StrokeAlign align);

  static std::string TextAlignToString(TextAlign align);

  static std::string TileModeToString(TileMode tileMode);

  static std::string ImageTypeToString(Types::ImageType type);

  static std::string FilterModeToString(FilterMode mode);

  static std::string MipmapModeToString(MipmapMode mode);

  static std::string ShapeTypeToString(Types::ShapeType type);

  static std::string ShaderTypeToString(Types::ShaderType type);

  static std::string LineCapToString(LineCap lineCap);

  static std::string LineJoinToString(LineJoin lineJoin);

  static std::string ImageFilterTypeToString(Types::ImageFilterType type);

  static std::string ColorFilterTypeToString(Types::ColorFilterType type);

  static std::string LayerFilterTypeToString(Types::LayerFilterType type);

  static std::string LayerStyleTypeToString(LayerStyleType type);

  static std::string LayerStylePositionToString(LayerStylePosition position);

  static std::string LayerStyleExtraSourceTypeToString(LayerStyleExtraSourceType type);

  static std::string ShapeStyleTypeToString(Types::ShapeStyleType type);

  static std::string GradientTypeToString(GradientType type);

  static std::string PathFillTypeToString(PathFillType type);

  static void SerializeBegin(flexbuffers::Builder& fbb, const std::string& type, size_t& mapStart,
                             size_t& contentStart);

  static void SerializeEnd(flexbuffers::Builder& fbb, size_t mapStart, size_t contentStart);

  template <typename T>
  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, T value,
                               bool isAddress = false, bool isExpandable = false) {
    (void)fbb;
    (void)value;
    (void)key;
    (void)isExpandable;
    (void)isAddress;
    assert(false);
  }
};

}  // namespace tgfx