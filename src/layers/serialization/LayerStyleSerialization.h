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

namespace tgfx{
  class LayerStyle;
  class LayerStyleSerialization
  {
  public:
    UNCOPIED_PACK(LayerStyleSerialization)

    static std::shared_ptr<LayerStyleSerialization> GetSerialization(std::shared_ptr<LayerStyle> layerStyle);
    LayerStyleSerialization(std::shared_ptr<LayerStyle> layerStyle);
    virtual ~LayerStyleSerialization() = default;
    virtual uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder);
  protected:
    std::shared_ptr<LayerStyle> m_LayerStyle;
    tgfx::fbs::LayerStyleCommonAttribute m_CommonAttribute;
  };

  class BackGroundBlurStyleSerialization : public LayerStyleSerialization {
  public:
    UNCOPIED_PACK(BackGroundBlurStyleSerialization)

    BackGroundBlurStyleSerialization(std::shared_ptr<LayerStyle> layerStyle);
    virtual ~BackGroundBlurStyleSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class DropShadowStyleSerialization : public LayerStyleSerialization {
  public:
    UNCOPIED_PACK(DropShadowStyleSerialization)

    DropShadowStyleSerialization(std::shared_ptr<LayerStyle> layerStyle);
    virtual ~DropShadowStyleSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class InnerShadowStyleSerialization : public LayerStyleSerialization {
  public:
    UNCOPIED_PACK(InnerShadowStyleSerialization)

    InnerShadowStyleSerialization(std::shared_ptr<LayerStyle> layerStyle);
    virtual ~InnerShadowStyleSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };
}
