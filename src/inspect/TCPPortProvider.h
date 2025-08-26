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
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>

namespace tgfx::inspect {
class TCPPortProvider {
 public:
  static TCPPortProvider& Get() {
    static TCPPortProvider instance;
    return instance;
  }

  TCPPortProvider() = default;

  ~TCPPortProvider() = default;

  uint16_t getValidPort();

  TCPPortProvider(const TCPPortProvider& provider) = delete;

  TCPPortProvider(TCPPortProvider&& provider) = delete;

  TCPPortProvider& operator=(const TCPPortProvider& provider) = delete;

  TCPPortProvider& operator=(TCPPortProvider&& provider) = delete;

 private:
  std::set<uint16_t> usedPortSet = {};
  std::mutex mutex = {};
};
}  // namespace tgfx::inspect
