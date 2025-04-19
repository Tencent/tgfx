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

#include "OHOSTaskThread.h"
#include <sched.h>
#include <cstdint>

namespace tgfx {

TaskThread* TaskThread::Create() {
  auto maxCount = MaxThreadCount();
  return new OHOSTaskThread(
      static_cast<uint64_t>(maxCount - TaskGroup::GetInstance()->totalThreads));
}

void OHOSTaskThread::preRun() {
  // Set the thread affinity to the specified CPU core
  if (cpuMask != 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuMask, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
  }
}
}  // namespace tgfx