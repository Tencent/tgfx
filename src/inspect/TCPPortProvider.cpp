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

#include "TCPPortProvider.h"
#include <cassert>
#include <random>
#include "Socket.h"

namespace tgfx::inspect {
static constexpr uint16_t MaxPort = 65535;
static constexpr uint16_t MinPort = 49152;

uint16_t TCPPortProvider::getValidPort() {
  std::lock_guard<std::mutex> lock(mutex);
  uint32_t rangeSize = MaxPort - MinPort + 1;
  if (rangeSize == usedPortSet.size()) {
    usedPortSet.clear();
  }
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint16_t> dis(MinPort, MaxPort);
  uint16_t randomPort = dis(gen);
  std::unique_ptr<ListenSocket> listenSocket = std::make_unique<ListenSocket>();
  while (usedPortSet.find(randomPort) != usedPortSet.end()) {
    randomPort = dis(gen);
  }
  if (!listenSocket->listenSock(randomPort, 4)) {
    return 0;
  }
  if (usedPortSet.find(randomPort) != usedPortSet.end()) {
    usedPortSet.insert(randomPort);
  }
  return randomPort;
}
}  // namespace tgfx::inspect
