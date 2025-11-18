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

#include "tgfx/core/Task.h"
#include "utils/Log.h"

namespace tgfx {
template <typename T>
class DataWrapper;

template <typename T>
class AsyncDataSource;

/**
 * DataSource defers the data loading until it is required.
 */
template <typename T>
class DataSource {
 public:
  /**
   * Wraps the existing data into a DataSource.
   */
  static std::unique_ptr<DataSource> Wrap(std::shared_ptr<T> data) {
    if (data == nullptr) {
      return nullptr;
    }
    return std::make_unique<DataWrapper<T>>(std::move(data));
  }

  /**
   * Wraps the existing data source into an asynchronous DataSource and starts loading the data
	 * immediately.
   */
  static std::unique_ptr<DataSource> Async(std::unique_ptr<DataSource> source) {
#ifndef TGFX_USE_THREADS
    return source;
#endif
    if (source == nullptr) {
      return nullptr;
    }
    return std::make_unique<AsyncDataSource<T>>(std::move(source));
  }

  virtual ~DataSource() = default;

  /**
   * Returns the data. DataSource does not cache the data, each call to getData() will
   * generate a new data.
   */
  virtual std::shared_ptr<T> getData() const = 0;
};

/**
 * DataSourceWrapper wraps the existing data into a DataSource.
 */
template <typename T>
class DataWrapper : public DataSource<T> {
 public:
  explicit DataWrapper(std::shared_ptr<T> data) : data(std::move(data)) {
  }

  std::shared_ptr<T> getData() const override {
    return data;
  }

 private:
  std::shared_ptr<T> data = nullptr;
};

/**
 * DataTask is a task that loads the data from a data source.
 */
template <typename T>
class DataTask : public Task {
 public:
  explicit DataTask(std::unique_ptr<DataSource<T>> source) : source(std::move(source)) {
  }

  std::shared_ptr<T> getData() {
    return data;
  }

 protected:
  void onExecute() override {
    DEBUG_ASSERT(source != nullptr);
    data = source->getData();
    source = nullptr;
  }

  void onCancel() override {
    source = nullptr;
  }

 private:
  std::shared_ptr<T> data = nullptr;
  std::unique_ptr<DataSource<T>> source = nullptr;
};

/**
 * AsyncDataSource wraps the existing data source into an asynchronous DataSource and starts loading
 * the data immediately.
 */
template <typename T>
class AsyncDataSource : public DataSource<T> {
 public:
  explicit AsyncDataSource(std::unique_ptr<DataSource<T>> source) {
    task = std::make_shared<DataTask<T>>(std::move(source));
    Task::Run(task);
  }

  ~AsyncDataSource() override {
    task->cancel();
  }

  std::shared_ptr<T> getData() const override {
    task->wait();
    return task->getData();
  }

 private:
  std::shared_ptr<DataTask<T>> task = nullptr;
};
}  // namespace tgfx
