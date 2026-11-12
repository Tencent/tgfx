/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "core/filters/TextureImageFilter.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"

namespace tgfx {

class TextureGrainFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<TextureGrainFragmentProcessor> Make(BlockAllocator* allocator,
                                                          std::shared_ptr<TextureProxy> source,
                                                          std::shared_ptr<TextureView> gradientView,
                                                          const TextureGrainParams& params,
                                                          const SamplingOptions& sampling,
                                                          bool useHardwareDecal,
                                                          const Matrix& coordMatrix = Matrix::I());

  std::string name() const override {
    return "TextureGrainFragmentProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  size_t onCountTextureSamplers() const override;

  std::shared_ptr<Texture> onTextureAt(size_t index) const override;

  SamplerState onSamplerStateAt(size_t index) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  TextureGrainFragmentProcessor(std::shared_ptr<TextureProxy> source,
                                std::shared_ptr<TextureView> gradientView,
                                const TextureGrainParams& params, const SamplingOptions& sampling,
                                bool useHardwareDecal, const Matrix& coordMatrix);

  TextureView* getTextureView() const;

  YUVTextureView* getYUVTexture() const;

  size_t sourceSamplerCount() const;

  std::shared_ptr<TextureProxy> sourceProxy;
  std::shared_ptr<TextureView> gradientView;
  TextureGrainParams params = {};
  SamplingOptions sampling = {};
  bool useHardwareDecal = false;
  CoordTransform coordTransform;

  friend class GLSLTextureGrainFragmentProcessor;
};

}  // namespace tgfx
