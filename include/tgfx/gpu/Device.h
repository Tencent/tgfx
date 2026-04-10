/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <atomic>
#include <memory>
#include <mutex>
#include "tgfx/core/Matrix.h"

namespace tgfx {
class Context;
class GPU;

/**
 * The GPU interface for drawing graphics.
 */
class Device {
 public:
  virtual ~Device();

  /**
   * Returns a global unique ID for this device.
   */
  uint32_t uniqueID() const {
    return _uniqueID;
  }

  /**
   * Locks the rendering context associated with this device. If another thread has already locked
   * the device by lockContext(), a call to lockContext() will block execution until the device
   * is available. The returned context can be used to draw graphics. A nullptr is returned if the
   * context cannot be locked on the calling thread (e.g., the GPU context has been permanently lost
   * due to a GPU reset), and leaves the device unlocked.
   */
  Context* lockContext();

  /**
   * Unlocks the device. After this method is called all subsequent calls on the previously returned
   * context will be invalid and may lead to runtime crash.
   */
  void unlock();

 protected:
  std::mutex locker = {};
  Context* context = nullptr;
  std::weak_ptr<Device> weakThis;
  /**
   * A permanent one-way flag indicating the GPU context has been irreversibly lost (e.g., due to a
   * GPU reset). Once set to true, lockContext() will always return nullptr.
   */
  std::atomic<bool> _contextLost{false};

  explicit Device(std::unique_ptr<GPU> gpu);
  void releaseAll();
  virtual bool onLockContext();
  virtual void onUnlockContext();

 private:
  uint32_t _uniqueID = 0;
  GPU* _gpu = nullptr;
  bool contextLocked = false;

  friend class ResourceCache;
};
}  // namespace tgfx
