/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <memory>
#include <string>

namespace tgfx {
class Data;
/**
 * Base class for loading data via network or local path created externally to tgfx.
 */
class DataLoader {
 public:
  virtual ~DataLoader() = default;
  /**
   * Load a file from a network or local path, return null if the file does not exist.
   */
  virtual std::shared_ptr<Data> makeFromFile(const std::string& filePath) = 0;
};

/**
 * Data holds an immutable data buffer. Not only is the Data immutable, but the actual pointer
 * returned by data() or bytes() is guaranteed to always be the same for the life of this instance.
 */
class Data {
 public:
  /**
   * Registers an external data loader, which can used to load data from a network or local path.
   */
  static void RegisterExternalDataLoader(std::unique_ptr<DataLoader> loader);

  /**
   * Returns true if an external data loader is registered.
   */
  static bool HasExternalDataLoader();

  /**
   * Creates a Data object from the specified file path.
   */
  static std::shared_ptr<Data> MakeFromFile(const std::string& filePath);

  /**
   * Creates a Data object by copying the specified data.
   */
  static std::shared_ptr<Data> MakeWithCopy(const void* data, size_t length);

  /**
   * Call this when the data parameter is already const, suitable for const globals. The caller must
   * ensure the data parameter will always be the same and alive for the lifetime of the returned
   * Data.
   */
  static std::shared_ptr<Data> MakeWithoutCopy(const void* data, size_t length);

  /**
   * Function that, if provided, will be called when the Data goes out of scope, allowing for
   * custom allocation/freeing of the data's contents.
   */
  typedef void (*ReleaseProc)(const void* data, void* context);

  /**
   * A ReleaseProc using delete to release data.
   */
  static void DeleteProc(const void* data, void* context);

  /**
   * A ReleaseProc using free() to release data.
   */
  static void FreeProc(const void* data, void* context);

  /**
   * Creates a Data object, taking ownership of the specified data, and using the releaseProc to
   * free it. The releaseProc may be nullptr.
   */
  static std::shared_ptr<Data> MakeAdopted(const void* data, size_t length,
                                           ReleaseProc releaseProc = DeleteProc,
                                           void* context = nullptr);

  /**
   *  Creates a new empty Data object.
   */
  static std::shared_ptr<Data> MakeEmpty();

  ~Data();

  /**
   * Returns the memory address of the data.
   */
  const void* data() const {
    return _data;
  }

  /**
   * Returns the read-only memory address of the data, but in this case it is cast to uint8_t*, to
   * make it easy to add an offset to it.
   */
  const uint8_t* bytes() const {
    return reinterpret_cast<const uint8_t*>(_data);
  }

  /**
   * Returns the byte size.
   */
  size_t size() const {
    return _size;
  }

  /**
   * Returns true if the Data is empty.
   */
  bool empty() const {
    return _size == 0;
  }

 private:
  const void* _data = nullptr;
  size_t _size = 0;
  ReleaseProc releaseProc = nullptr;
  void* releaseContext = nullptr;

  Data(const void* data, size_t length, ReleaseProc releaseProc, void* context);
};

}  // namespace tgfx
