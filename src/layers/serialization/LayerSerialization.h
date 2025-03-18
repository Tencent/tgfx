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

#include "flatbuffers/flatbuffers.h"

namespace tgfx {
  class Layer;
  class LayerSerialization
  {
  public:
    static std::vector<uint8_t> serializingLayerAttribute(std::shared_ptr<Layer> layer);
    static std::vector<uint8_t> serializingTreeNode(std::shared_ptr<Layer> layer);

    LayerSerialization() = delete;
    LayerSerialization(const LayerSerialization& ) = delete;
    LayerSerialization(LayerSerialization&&) = delete;
    LayerSerialization& operator=(const LayerSerialization&) = delete;
    LayerSerialization& operator=(LayerSerialization&&) = delete;

    static std::shared_ptr<LayerSerialization> GetSerialization(std::shared_ptr<Layer> layer);
    LayerSerialization(std::shared_ptr<tgfx::Layer> layer);
    virtual ~LayerSerialization() = default;
    virtual uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder);
    uint32_t SerializationLayerTreeNode(flatbuffers::FlatBufferBuilder& builder);
  private:
    uint32_t createTreeNode(flatbuffers::FlatBufferBuilder& builder, std::shared_ptr<Layer> layer);
  protected:
    std::shared_ptr<Layer> m_Layer;
  };

  class BasicLayerSerialization : public LayerSerialization {
  public:
    BasicLayerSerialization() = delete;
    BasicLayerSerialization(const BasicLayerSerialization& ) = delete;
    BasicLayerSerialization(BasicLayerSerialization&&) = delete;
    BasicLayerSerialization& operator=(const BasicLayerSerialization&) = delete;
    BasicLayerSerialization& operator=(BasicLayerSerialization&&) = delete;
    BasicLayerSerialization(std::shared_ptr<Layer> layer);
    virtual ~BasicLayerSerialization() = default;
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class ImageLayerSerialization : public LayerSerialization {
  public:
    ImageLayerSerialization() = delete;
    ImageLayerSerialization(const ImageLayerSerialization& ) = delete;
    ImageLayerSerialization(ImageLayerSerialization&&) = delete;
    ImageLayerSerialization& operator=(const ImageLayerSerialization&) = delete;
    ImageLayerSerialization& operator=(ImageLayerSerialization&&) = delete;
    ImageLayerSerialization(std::shared_ptr<Layer> layer);
    virtual ~ImageLayerSerialization() = default;
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class ShapeLayerSerialization : public LayerSerialization {
  public:
    ShapeLayerSerialization() = delete;
    ShapeLayerSerialization(const ShapeLayerSerialization& ) = delete;
    ShapeLayerSerialization(ShapeLayerSerialization&&) = delete;
    ShapeLayerSerialization& operator=(const ShapeLayerSerialization&) = delete;
    ShapeLayerSerialization& operator=(ShapeLayerSerialization&&) = delete;
    ShapeLayerSerialization(std::shared_ptr<Layer> layer);
    virtual ~ShapeLayerSerialization() = default;
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class SolidLayerSerialization : public LayerSerialization {
  public:
    SolidLayerSerialization() = delete;
    SolidLayerSerialization(const SolidLayerSerialization& ) = delete;
    SolidLayerSerialization(SolidLayerSerialization&&) = delete;
    SolidLayerSerialization& operator=(const SolidLayerSerialization&) = delete;
    SolidLayerSerialization& operator=(SolidLayerSerialization&&) = delete;
    SolidLayerSerialization(std::shared_ptr<Layer> layer);
    virtual ~SolidLayerSerialization() = default;
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class TextLayerSerialization : public LayerSerialization {
  public:
    TextLayerSerialization() = delete;
    TextLayerSerialization(const TextLayerSerialization& ) = delete;
    TextLayerSerialization(TextLayerSerialization&&) = delete;
    TextLayerSerialization& operator=(const TextLayerSerialization&) = delete;
    TextLayerSerialization& operator=(TextLayerSerialization&&) = delete;
    TextLayerSerialization(std::shared_ptr<Layer> layer);
    virtual ~TextLayerSerialization() = default;
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };
}

