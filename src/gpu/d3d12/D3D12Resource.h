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

#include <list>
#include "core/utils/ReturnQueue.h"

namespace tgfx {

class D3D12GPU;

/**
 * Base class for D3D12 GPU resources. Subclasses must implement the onRelease() method to free all
 * underlying GPU resources. No D3D12 API calls should be made during destruction since the resource
 * may be destroyed on any thread.
 */
class D3D12Resource : public ReturnNode {
 protected:
  /**
   * Overridden to free the underlying D3D12 resources. After calling this method, the D3D12Resource
   * must not be used, as doing so may lead to undefined behavior.
   */
  virtual void onRelease(D3D12GPU* gpu) = 0;

 private:
  std::list<D3D12Resource*>::iterator cachedPosition;

  friend class D3D12GPU;
};

}  // namespace tgfx
