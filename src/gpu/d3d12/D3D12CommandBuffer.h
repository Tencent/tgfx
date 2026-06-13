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

#include "gpu/d3d12/D3D12FrameSession.h"
#include "tgfx/gpu/CommandBuffer.h"

namespace tgfx {

class D3D12GPU;

/**
 * Transport container that carries a D3D12FrameSession from encoding to submission.
 *
 * Created by D3D12CommandEncoder::onFinish() which moves its D3D12FrameSession here. Consumed by
 * D3D12CommandQueue::submit() which moves the session into D3D12GPU's InflightSubmission. After
 * submit(), this object is empty and may be discarded.
 *
 * If the CommandBuffer is abandoned (created but never submitted), the destructor reclaims all
 * session resources via D3D12GPU::reclaimAbandonedSession(). This matches the abandon safety
 * guarantee provided by D3D12CommandEncoder::onRelease() — both use the same unified cleanup path
 * in D3D12GPU, ensuring no D3D12 objects are leaked regardless of where the pipeline is
 * interrupted.
 */
class D3D12CommandBuffer : public CommandBuffer {
 public:
  D3D12CommandBuffer(D3D12GPU* gpu, D3D12FrameSession session)
      : _gpu(gpu), session(std::move(session)) {
  }

  ~D3D12CommandBuffer() override;

  D3D12FrameSession& frameSession() {
    return session;
  }

  ID3D12GraphicsCommandList* d3d12CommandList() const {
    return session.commandList.Get();
  }

  ID3D12CommandAllocator* d3d12CommandAllocator() const {
    return session.commandAllocator.Get();
  }

 private:
  D3D12GPU* _gpu = nullptr;
  D3D12FrameSession session;
};

}  // namespace tgfx
