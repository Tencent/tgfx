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

#include "D3D12Util.h"

namespace tgfx {

class D3D12GPU;
class D3D12Texture;

/// Number of threads per group on each axis. The compute shader is `[numthreads(8, 8, 1)]`, so
/// dispatching encoders need to round the output mip dimensions up to this value.
static constexpr unsigned D3D12_MIPMAP_THREAD_GROUP_SIZE = 8;

/**
 * Lazily-initialised compute pipeline that downsamples mip[i] into mip[i+1] for a 2D texture.
 *
 * D3D12 has no built-in equivalent to Metal's [blitEncoder generateMipmapsForTexture] or
 * Vulkan's vkCmdBlitImage chain, so we ship a tiny box-filter compute shader and dispatch it
 * per mip level. The PSO and root signature are cached on the GPU so repeated mipmap generation
 * doesn't repeatedly recompile the shader.
 *
 * Root signature layout:
 *   slot 0: 4 32-bit root constants (output mip width, height, inv_width, inv_height)
 *   slot 1: SRV descriptor table referencing mip[i]
 *   slot 2: UAV descriptor table referencing mip[i+1]
 *
 * The encoder is expected to:
 *   - Place the SRV / UAV pair into a shader-visible heap that lives long enough to cover the
 *     dispatch (added to the FrameSession's retainedDescriptorHeaps).
 *   - Issue ResourceBarriers transitioning the parent texture's individual subresources between
 *     UNORDERED_ACCESS and PIXEL_SHADER_RESOURCE as the chain walks up.
 */
class D3D12MipmapGenerator {
 public:
  static D3D12MipmapGenerator* Get(D3D12GPU* gpu);

  ID3D12RootSignature* rootSignature() const {
    return _rootSignature.Get();
  }

  ID3D12PipelineState* pipelineState() const {
    return _pipelineState.Get();
  }

  /**
   * Returns true once both the root signature and the pipeline state are ready. Callers should
   * skip mipmap generation when this is false (and emit a one-time log).
   */
  bool isReady() const {
    return _rootSignature != nullptr && _pipelineState != nullptr;
  }

 private:
  explicit D3D12MipmapGenerator(D3D12GPU* gpu);

  bool createRootSignature(D3D12GPU* gpu);
  bool createPipelineState(D3D12GPU* gpu);

  ComPtr<ID3D12RootSignature> _rootSignature;
  ComPtr<ID3D12PipelineState> _pipelineState;

  friend class D3D12GPU;
};

}  // namespace tgfx
