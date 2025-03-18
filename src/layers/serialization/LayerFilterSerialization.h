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
//#include "SerializationStructure.h"
#include "layers/generate/SerializationStructure_generated.h"
#include <flatbuffers/flatbuffer_builder.h>
namespace tgfx {
  class LayerFilter;
  class LayerFilterSerialization
  {
  public:
    LayerFilterSerialization() = delete;
    LayerFilterSerialization(const LayerFilterSerialization& ) = delete;
    LayerFilterSerialization(LayerFilterSerialization&&) = delete;
    LayerFilterSerialization& operator=(const LayerFilterSerialization&) = delete;
    LayerFilterSerialization& operator=(LayerFilterSerialization&&) = delete;

    static std::shared_ptr<LayerFilterSerialization> GetSerialization(std::shared_ptr<LayerFilter> layerFilter);

    LayerFilterSerialization(std::shared_ptr<LayerFilter> LayerFilter);
    virtual ~LayerFilterSerialization() = default;
    virtual uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder);
  protected:
    fbs::LayerfilterCommonAttribute m_LayerfilterCommonAttribute;
    std::shared_ptr<LayerFilter> m_LayerFilter;
  };

  class BlendFilterSerialization : public LayerFilterSerialization {
  public:
    BlendFilterSerialization() = delete;
    BlendFilterSerialization(const BlendFilterSerialization& ) = delete;
    BlendFilterSerialization(BlendFilterSerialization&&) = delete;
    BlendFilterSerialization& operator=(const BlendFilterSerialization&) = delete;
    BlendFilterSerialization& operator=(BlendFilterSerialization&&) = delete;

    BlendFilterSerialization(std::shared_ptr<LayerFilter> layerFilter);
    virtual ~BlendFilterSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class BlurFilterSerialization : public LayerFilterSerialization {
  public:
    BlurFilterSerialization() = delete;
    BlurFilterSerialization(const BlurFilterSerialization& ) = delete;
    BlurFilterSerialization(BlurFilterSerialization&&) = delete;
    BlurFilterSerialization& operator=(const BlurFilterSerialization&) = delete;
    BlurFilterSerialization& operator=(BlurFilterSerialization&&) = delete;

    BlurFilterSerialization(std::shared_ptr<LayerFilter> layerFilter);
    virtual ~BlurFilterSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class ColorMatrixFilterSerialization : public LayerFilterSerialization {
  public:
    ColorMatrixFilterSerialization() = delete;
    ColorMatrixFilterSerialization(const ColorMatrixFilterSerialization& ) = delete;
    ColorMatrixFilterSerialization(ColorMatrixFilterSerialization&&) = delete;
    ColorMatrixFilterSerialization& operator=(const ColorMatrixFilterSerialization&) = delete;
    ColorMatrixFilterSerialization& operator=(ColorMatrixFilterSerialization&&) = delete;

    ColorMatrixFilterSerialization(std::shared_ptr<LayerFilter> layerFilter);
    virtual ~ColorMatrixFilterSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class DropShadowFilterSerialization : public LayerFilterSerialization {
  public:
    DropShadowFilterSerialization() = delete;
    DropShadowFilterSerialization(const DropShadowFilterSerialization& ) = delete;
    DropShadowFilterSerialization(DropShadowFilterSerialization&&) = delete;
    DropShadowFilterSerialization& operator=(const DropShadowFilterSerialization&) = delete;
    DropShadowFilterSerialization& operator=(DropShadowFilterSerialization&&) = delete;

    DropShadowFilterSerialization(std::shared_ptr<LayerFilter> layerFilter);
    virtual ~DropShadowFilterSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };

  class InnerShadowFilterSerialization : public LayerFilterSerialization {
  public:
    InnerShadowFilterSerialization() = delete;
    InnerShadowFilterSerialization(const InnerShadowFilterSerialization& ) = delete;
    InnerShadowFilterSerialization(InnerShadowFilterSerialization&&) = delete;
    InnerShadowFilterSerialization& operator=(const InnerShadowFilterSerialization&) = delete;
    InnerShadowFilterSerialization& operator=(InnerShadowFilterSerialization&&) = delete;

    InnerShadowFilterSerialization(std::shared_ptr<LayerFilter> layerFilter);
    virtual ~InnerShadowFilterSerialization() = default;
    uint32_t Serialization(flatbuffers::FlatBufferBuilder& builder) override;
  };
}
