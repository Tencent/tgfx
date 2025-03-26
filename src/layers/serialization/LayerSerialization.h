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
#include "SerializationUtils.h"

namespace tgfx {
  class Layer;

  class LayerSerialization{
  public:
    UNCOPIED_PACK(LayerSerialization);

    static std::vector<uint8_t> serializingLayerAttribute(std::shared_ptr<Layer> layer);
    static std::vector<uint8_t> serializingTreeNode(std::shared_ptr<Layer> layer);

    static std::shared_ptr<LayerSerialization> GetSerialization(std::shared_ptr<Layer> layer);

    virtual ~LayerSerialization() = default;

    virtual std::string OriginClassName() = 0;
    virtual uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder);
    uint32_t SerializationLayerTreeNode(flatbuffers::FlatBufferBuilder& builder, std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap);
  private:
    uint32_t createTreeNode(flatbuffers::FlatBufferBuilder& builder, std::shared_ptr<Layer> layer, std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap);
  protected:
    explicit LayerSerialization(std::shared_ptr<tgfx::Layer> layer);
    std::shared_ptr<Layer> m_Layer;
  };

  class BasicLayerSerialization : public LayerSerialization {
  public:
    UNCOPIED_PACK(BasicLayerSerialization);

    explicit BasicLayerSerialization(std::shared_ptr<Layer> layer);
    ~BasicLayerSerialization() override = default;
    std::string OriginClassName() override {
      return "Layer";
    }
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class ImageLayerSerialization : public LayerSerialization {
  public:
    UNCOPIED_PACK(ImageLayerSerialization);

    explicit ImageLayerSerialization(std::shared_ptr<Layer> layer);
    ~ImageLayerSerialization() override = default;
    std::string OriginClassName() override {
      return "ImageLayer";
    }
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class ShapeLayerSerialization : public LayerSerialization {
  public:
    UNCOPIED_PACK(ShapeLayerSerialization);

    explicit ShapeLayerSerialization(std::shared_ptr<Layer> layer);
    ~ShapeLayerSerialization() override = default;
    std::string OriginClassName() override {
      return "ShapeLayer";
    }
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class SolidLayerSerialization : public LayerSerialization {
  public:
    UNCOPIED_PACK(SolidLayerSerialization);

    explicit SolidLayerSerialization(std::shared_ptr<Layer> layer);
    ~SolidLayerSerialization() override = default;
    std::string OriginClassName() override {
      return "SolidLayer";
    }
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class TextLayerSerialization : public LayerSerialization {
  public:
    UNCOPIED_PACK(TextLayerSerialization);

    explicit TextLayerSerialization(std::shared_ptr<Layer> layer);
    ~TextLayerSerialization() override = default;
    std::string OriginClassName() override {
      return "TextLayer";
    }
    uint32_t SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) override;
  };
}

