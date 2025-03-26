/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "SerializationUtils.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "layers/generate/SerializationStructure_generated.h"

namespace tgfx {
 class ShapeStyle;
 class ShapeStyleSerialization {
 public:
   UNCOPIED_PACK(ShapeStyleSerialization)

   static std::shared_ptr<ShapeStyleSerialization> GetSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
   ShapeStyleSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
   virtual ~ShapeStyleSerialization() = default;
   virtual uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder);
 protected:
   std::shared_ptr<ShapeStyle> m_ShapeStyle;
   fbs::ShapeStyleCommonAttribute m_ShapeStyleCommonAttribute;
 };

  class ImagePatternStyleSerialization : public ShapeStyleSerialization {
  public:
    UNCOPIED_PACK(ImagePatternStyleSerialization)

    ImagePatternStyleSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
    virtual ~ImagePatternStyleSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };

 class GradientStyleSerialization : public ShapeStyleSerialization {
 public:
    UNCOPIED_PACK(GradientStyleSerialization)

   GradientStyleSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
   virtual ~GradientStyleSerialization() = default;
   uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
 };

 class LinearGradientSerialization : public GradientStyleSerialization {
 public:
   UNCOPIED_PACK(LinearGradientSerialization)

   LinearGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
   virtual ~LinearGradientSerialization() = default;
   uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
 };

class RadialGradientSerialization : public GradientStyleSerialization {
public:
  UNCOPIED_PACK(RadialGradientSerialization)

  RadialGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
  virtual ~RadialGradientSerialization() = default;
  uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
};

class ConicGradientSerialization : public GradientStyleSerialization {
public:
  UNCOPIED_PACK(ConicGradientSerialization)

  ConicGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
  virtual ~ConicGradientSerialization() = default;
  uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
};

class DiamondGradientSerialization : public GradientStyleSerialization {
public:
  UNCOPIED_PACK(DiamondGradientSerialization)

  DiamondGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle);
  virtual ~DiamondGradientSerialization() = default;
  uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
};

}
