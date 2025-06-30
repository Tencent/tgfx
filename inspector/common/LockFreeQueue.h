#pragma once
#include <atomic>
#include <iostream>
#include <memory>

namespace inspector {
template <typename T>
class LockFreeQueue {
 private:
  struct Node {
    std::shared_ptr<T> data;  // 存储数据
    std::atomic<Node*> next;  // 指向下一个节点

    Node(T value) : data(std::make_shared<T>(value)), next(nullptr) {
    }
  };

  std::atomic<Node*> head;  // 队列头指针
  std::atomic<Node*> tail;  // 队列尾指针

 public:
  LockFreeQueue() {
    Node* dummy = new Node(T());  // 创建一个虚拟节点
    head.store(dummy);
    tail.store(dummy);
  }

  ~LockFreeQueue() {
    while (Node* old_head = head.load()) {
      head.store(old_head->next);
      delete old_head;
    }
  }

  // 入队操作
  void push(T value) {
    Node* new_node = new Node(value);
    while (true) {
      Node* old_tail = tail.load();
      Node* next = old_tail->next.load();
      if (old_tail == tail.load()) {  // 检查 tail 是否被其他线程修改
        if (next == nullptr) {        // 如果 tail 的 next 为空
          if (old_tail->next.compare_exchange_weak(next, new_node)) {
            // 将新节点插入到 tail 的 next
            tail.compare_exchange_weak(old_tail, new_node);  // 更新 tail
            return;
          }
        } else {
          tail.compare_exchange_weak(old_tail, next);  // 帮助其他线程更新 tail
        }
      }
    }
  }

  // 出队操作
  std::shared_ptr<T> pop() {
    while (true) {
      Node* old_head = head.load();
      Node* old_tail = tail.load();
      Node* next = old_head->next.load();
      if (old_head == head.load()) {  // 检查 head 是否被其他线程修改
        if (old_head == old_tail) {   // 队列为空或 tail 未更新
          if (next == nullptr) {
            return nullptr;  // 队列为空
          }
          tail.compare_exchange_weak(old_tail, next);  // 帮助更新 tail
        } else {
          std::shared_ptr<T> res = next->data;  // 获取队首数据
          if (head.compare_exchange_weak(old_head, next)) {
            // 更新 head 到下一个节点
            delete old_head;  // 释放旧的头节点
            return res;
          }
        }
      }
    }
  }

  // 获取队首元素（不删除）
  std::shared_ptr<T> front() {
    while (true) {
      Node* old_head = head.load();
      Node* next = old_head->next.load();
      if (old_head == head.load()) {  // 检查 head 是否被其他线程修改
        if (next == nullptr) {
          return nullptr;  // 队列为空
        }
        return next->data;  // 返回队首数据
      }
    }
  }

  // 检查队列是否为空
  bool empty() {
    Node* old_head = head.load();
    Node* old_tail = tail.load();
    Node* next = old_head->next.load();
    return (old_head == old_tail && next == nullptr);  // 队列为空的条件
  }
};
}  // namespace inspector