/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "ResolvService.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

namespace inspector {
ResolvService::ResolvService(uint16_t port)
    : exit(false), port(port), thread([this] { Worker(); }) {
}

ResolvService::~ResolvService() {
  exit.store(true, std::memory_order_relaxed);
  conditionVariable.notify_one();
  thread.join();
}

void ResolvService::Query(uint32_t ip, const std::function<void(std::string&&)>& callback) {
  std::lock_guard<std::mutex> lock(mutex);
  queue.emplace_back(QueueItem{ip, callback});
  conditionVariable.notify_one();
}

void ResolvService::Worker() {
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  char buf[128];

  for (;;) {
    std::unique_lock<std::mutex> lock(mutex);
    conditionVariable.wait(
        lock, [this] { return !queue.empty() || exit.load(std::memory_order_relaxed); });
    if (exit.load(std::memory_order_relaxed)) return;
    auto query = queue.back();
    queue.pop_back();
    lock.unlock();

    addr.sin_addr.s_addr = query.ip;
    if (getnameinfo((const struct sockaddr*)&addr, sizeof(sockaddr_in), buf, 128, nullptr, 0,
                    NI_NOFQDN) != 0) {
      inet_ntop(AF_INET, &query.ip, buf, 17);
    }
    query.callback(buf);
  }
}
}  // namespace inspector