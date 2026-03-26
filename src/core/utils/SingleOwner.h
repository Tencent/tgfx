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

#include "core/utils/Log.h"

#if DEBUG

#include <thread>

namespace tgfx {

/**
 * Verifies that a protected object is only accessed from the thread that currently owns it. Call
 * acquire() to claim ownership and release() to relinquish it. Use ASSERT_SINGLE_OWNER at the
 * entry of every protected method to catch cross-thread and use-after-release bugs in debug builds.
 */
class SingleOwner {
 public:
  void acquire() {
    ownerThread = std::this_thread::get_id();
  }

  void release() {
    ownerThread = std::thread::id{};
  }

  bool isOwnerThread() const {
    return ownerThread == std::this_thread::get_id();
  }

 private:
  std::thread::id ownerThread = {};
};

}  // namespace tgfx

#define ASSERT_SINGLE_OWNER(owner) DEBUG_ASSERT((owner).isOwnerThread())
#define SINGLE_OWNER_ACQUIRE(owner) (owner).acquire()
#define SINGLE_OWNER_RELEASE(owner) (owner).release()

#else

#define ASSERT_SINGLE_OWNER(owner)
#define SINGLE_OWNER_ACQUIRE(owner)
#define SINGLE_OWNER_RELEASE(owner)

#endif
