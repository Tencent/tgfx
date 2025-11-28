/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
namespace tgfx {
class Once {
 public:
  constexpr Once() = default;

  template <typename Fn, typename... Args>
  void operator()(Fn&& fn, Args&&... args) {
    auto state = fState.load(std::memory_order_acquire);

    if (state == Done) {
      return;
    }

    // If it looks like no one has started calling fn(), try to claim that job.
    if (state == NotStarted &&
        fState.compare_exchange_strong(state, Claimed, std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
      // Great!  We'll run fn() then notify the other threads by releasing Done into fState.
      fn(std::forward<Args>(args)...);
      return fState.store(Done, std::memory_order_release);
    }

    // Some other thread is calling fn().
    while (fState.load(std::memory_order_acquire) != Done) { /*spin*/
    }
  }

  /**
   * Resets the Once to its initial state, allowing the function to be called again.
   * This method is not thread-safe and should only be called when no other threads
   * are accessing this Once object.
   */
  void reset() {
    fState.store(NotStarted, std::memory_order_release);
  }

 private:
  enum State : uint8_t { NotStarted, Claimed, Done };
  std::atomic<uint8_t> fState{NotStarted};
};
}  // namespace tgfx
