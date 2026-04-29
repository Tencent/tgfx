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

class VulkanGPU;

/**
 * Base class for Vulkan GPU resources. Subclasses must implement the onRelease() method to free all
 * underlying GPU resources. No Vulkan API calls should be made during destruction since the resource
 * may be destroyed on any thread.
 */
class VulkanResource : public ReturnNode {
 protected:
  virtual void onRelease(VulkanGPU* gpu) = 0;

 private:
  std::list<VulkanResource*>::iterator cachedPosition;

  friend class VulkanGPU;
};

}  // namespace tgfx
