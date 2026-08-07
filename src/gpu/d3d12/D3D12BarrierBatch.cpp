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

#include "D3D12BarrierBatch.h"

namespace tgfx {

void D3D12BarrierBatch::addTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                      D3D12_RESOURCE_STATES after, UINT subresource) {
  if (resource == nullptr || before == after) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = subresource;
  barriers.push_back(barrier);
}

void D3D12BarrierBatch::flush(ID3D12GraphicsCommandList* commandList) {
  if (barriers.empty() || commandList == nullptr) {
    barriers.clear();
    return;
  }
  commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
  barriers.clear();
}

}  // namespace tgfx
