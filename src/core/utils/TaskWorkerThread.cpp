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

#include "TaskWorkerThread.h"
#include <condition_variable>
#include <mutex>
#include "TaskGroup.h"

namespace tgfx {
static std::mutex locker = {};
static std::condition_variable condition = {};
static std::atomic_int waitingThreads = 0;

void TaskWorkerThread::NotifyNewTask() {
  if (waitingThreads > 0) {
    condition.notify_one();
  }
}

void TaskWorkerThread::NotifyExit() {
  condition.notify_all();
}

bool TaskWorkerThread::HasWaitingThread() {
  return waitingThreads > 0;
}

TaskWorkerThread* TaskWorkerThread::Create() {
  return new TaskWorkerThread();
}

TaskWorkerThread::~TaskWorkerThread() {
  if (!thread) {
    return;
  }
  if (!_exitWhileIdle) {
    if (thread->joinable()) {
      thread->join();
    }
  } else {
    thread->detach();
  }
  delete thread;
}

void TaskWorkerThread::exit() {
  exited = true;
}

void TaskWorkerThread::exitWhileIdle() {
  _exitWhileIdle = true;
}

bool TaskWorkerThread::start() {
  if (thread) {
    return true;
  }
  thread = new (std::nothrow) std::thread(ThreadProc, this);
  return thread != nullptr;
}

void TaskWorkerThread::ThreadProc(TaskWorkerThread* thread) {
  auto group = TaskGroup::GetInstance();
  std::unique_lock<std::mutex> autoLock(locker);
  while (!thread->exited) {
    auto task = group->popTask();
    if (!task) {
      if (thread->_exitWhileIdle) {
        // TaskGroup is not responsible for managing the lifecycle of threads that need to exit,
        // so we delete the thread here
        delete thread;
        break;
      }
      ++waitingThreads;
      condition.wait(autoLock);
      --waitingThreads;
      continue;
    }
    task->execute();
  }
}

}  // namespace tgfx
