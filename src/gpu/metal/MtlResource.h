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

class MtlGPU;

/**
 * Base class for Metal GPU resources. Subclasses must implement the onRelease() method to free all
 * underlying GPU resources. No Metal API calls should be made during destruction since the resource
 * may be destroyed on any thread.
 */
class MtlResource : public ReturnNode {
 protected:
  /**
   * Overridden to free the underlying Metal resources. After calling this method, the MtlResource
   * must not be used, as doing so may lead to undefined behavior.
   */
  virtual void onRelease(MtlGPU* gpu) = 0;

 private:
  std::list<MtlResource*>::iterator cachedPosition;

  friend class MtlGPU;
};

}  // namespace tgfx