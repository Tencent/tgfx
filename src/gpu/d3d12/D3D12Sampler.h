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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "D3D12Resource.h"
#include "D3D12Util.h"
#include "tgfx/gpu/Sampler.h"

namespace tgfx {

class D3D12GPU;

/**
 * D3D12 sampler implementation.
 */
class D3D12Sampler : public Sampler, public D3D12Resource {
 public:
  static std::shared_ptr<D3D12Sampler> Make(D3D12GPU* gpu, const SamplerDescriptor& descriptor);

  /**
   * Returns the D3D12 sampler description.
   */
  const D3D12_SAMPLER_DESC& samplerDesc() const {
    return _samplerDesc;
  }

 protected:
  void onRelease(D3D12GPU* gpu) override;

 private:
  explicit D3D12Sampler(const D3D12_SAMPLER_DESC& samplerDesc);
  ~D3D12Sampler() override = default;

  D3D12_SAMPLER_DESC _samplerDesc = {};

  friend class D3D12GPU;
};

}  // namespace tgfx
