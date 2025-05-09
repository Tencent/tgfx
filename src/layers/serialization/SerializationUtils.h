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

#include <flatbuffers/flexbuffers.h>
#include <tgfx/layers/Layer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/TextAlign.h>
#include <string>

namespace tgfx {
class SerializeUtils {
 public:
  static std::string layerTypeToString(LayerType type);
  static std::string blendModeToString(BlendMode mode);
  static std::string strokeAlignToString(StrokeAlign align);
  static std::string textAlignToString(TextAlign align);
  static std::string tileModeToString(TileMode tileMode);
  static std::string imageTypeToString(Image::Type type);
  static std::string filterModeToString(FilterMode mode);
  static std::string mipmapModeToString(MipmapMode mode);
  static std::string shapeTypeToString(Shape::Type type);
  static std::string shaderTypeToString(Shader::Type type);
  static std::string lineCapToString(LineCap lineCap);
  static std::string lineJoinToString(LineJoin lineJoin);
  static std::string imageFilterTypeToString(ImageFilter::Type type);
  static std::string colorFilterTypeToString(ColorFilter::Type type);

  static void serializeBegin(flexbuffers::Builder& fbb, const std::string& type, size_t& mapStart,
                             size_t& contentStart);
  static void serializeEnd(flexbuffers::Builder& fbb, size_t mapStart, size_t contentStart);

  template <typename T>
  static void setFlexBufferMap(flexbuffers::Builder& fbb, const char* key, T value,
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