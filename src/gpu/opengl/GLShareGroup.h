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

#include <memory>
#include <mutex>
#include <vector>

namespace tgfx {
/**
 * A GLShareGroup represents a set of OpenGL contexts that share the same object namespace (textures,
 * buffers, programs, etc.). The OpenGL ES specification (Appendix D "Shared Objects and Multiple
 * Contexts") requires the application to explicitly synchronize concurrent access to a shared
 * object from multiple contexts; failing to do so results in undefined behavior, which manifests as
 * cross-context rendering artifacts especially on GPU drivers with shallow command pipelines. To
 * fulfill this synchronization obligation inside the rendering engine, all GLDevices belonging to
 * the same share group share one std::mutex through Device::locker, so that lockContext() on any
 * member serializes with every other member of the same share group.
 *
 * Members are keyed by their native context handle. Two contexts that are equivalent under a
 * platform-specific share-group relation but have different handles (for example two EAGLContexts
 * with the same sharegroup obtained outside of GLDevice::Make) are NOT recognized as belonging to
 * the same group; callers that construct such contexts manually need to route them through
 * GLDevice::Make(sharedContext) to be included.
 */
class GLShareGroup : public std::enable_shared_from_this<GLShareGroup> {
 public:
  /**
   * Returns the share group that the given native context handle belongs to, or nullptr if the
   * handle has not been registered.
   */
  static std::shared_ptr<GLShareGroup> Find(void* nativeHandle);

  /**
   * Returns a share group that includes the given shared native context handle. If sharedHandle is
   * null, a new, independent share group is created and returned. Otherwise, the share group that
   * sharedHandle already belongs to is returned; if none exists yet, a new share group is created
   * and sharedHandle is registered as its first member so that subsequent lookups can find it.
   */
  static std::shared_ptr<GLShareGroup> GetOrCreate(void* sharedHandle);

  ~GLShareGroup();

  /**
   * The mutex shared by all GLDevices in this share group.
   */
  std::shared_ptr<std::mutex> locker() const {
    return _locker;
  }

  /**
   * Registers the native context handle as a member of this share group. Subsequent calls to
   * Find(nativeHandle) or GetOrCreate(nativeHandle) will return this share group.
   */
  void addMember(void* nativeHandle);

  /**
   * Unregisters the native context handle from this share group.
   */
  void removeMember(void* nativeHandle);

 private:
  std::shared_ptr<std::mutex> _locker;
  std::vector<void*> members = {};

  GLShareGroup();
};
}  // namespace tgfx
