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

#include "PThread.h"

namespace tgfx {

Thread* Thread::Create(std::function<void()> task, Priority priority) {
  return new PThread(std::move(task), priority);
}

PThread::~PThread() {
  if (PThread::joinable()) {
    pthread_detach(threadHandle);
  }
}

void* PThread::ThreadProc(void* arg) {
  auto thread = static_cast<PThread*>(arg);
  thread->task();
  return nullptr;
}

void PThread::onStart() {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  setPriorityAttributes(attr, priority);
  pthread_create(&threadHandle, &attr, &ThreadProc, nullptr);
  pthread_attr_destroy(&attr);
}

void PThread::onJoin() {
  pthread_join(threadHandle, nullptr);
  threadHandle = 0;
}

void PThread::setPriorityAttributes(pthread_attr_t& attr, Priority priority) {
  sched_param param{};
  int policy = SCHED_OTHER;
  pthread_attr_getschedpolicy(&attr, &policy);

  int minPriority = sched_get_priority_min(policy);
  int maxPriority = sched_get_priority_max(policy);
  int range = maxPriority - minPriority;

  switch (priority) {
    case Priority::Lowest:
      param.sched_priority = minPriority;
      break;
    case Priority::Low:
      param.sched_priority = minPriority + range / 4;
      break;
    case Priority::Normal:
      param.sched_priority = minPriority + range / 2;
      break;
    case Priority::High:
      param.sched_priority = minPriority + 3 * range / 4;
      break;
    case Priority::Highest:
      param.sched_priority = maxPriority;
      break;
  }

  pthread_attr_setschedpolicy(&attr, policy);
  pthread_attr_setschedparam(&attr, &param);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
}
}  // namespace tgfx