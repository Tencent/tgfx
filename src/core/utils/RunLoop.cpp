//
// Created by partyhuang on 2025/4/18.
//

#include "RunLoop.h"
#include "TaskGroup.h"

namespace tgfx {

RunLoop* RunLoop::Create() {
  return new RunLoop();
}

RunLoop::~RunLoop() {
  if (!thread) {
    return;
  }
  if (waitingWhileDestroy) {
    if (thread->joinable()) {
      thread->join();
    }
  } else {
    thread->detach();
  }
  delete thread;
}

void RunLoop::exit(bool wait) {
  waitingWhileDestroy = wait;
  exited = true;
}

bool RunLoop::start() {
  if (thread) {
    return true;
  }
  thread = new (std::nothrow) std::thread([this]() {
    while (!exited) {
      Execute();
    }
  });
  return thread != nullptr;
}

void RunLoop::Execute() {
  auto task = TaskGroup::GetInstance()->popTask();
  if (task == nullptr) {
    return;
  }
  task->execute();
}

}  // namespace tgfx
