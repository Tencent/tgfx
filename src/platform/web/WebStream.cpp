/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////


#include "WebStream.h"

namespace tgfx {

std::unique_ptr<Stream> WebStream::Make(const std::string& filePath) {
  val fileStreamClass = val::module_property("FileStream");
  if (!fileStreamClass.as<bool>()) {
    return nullptr;
  }
  auto fileStreamInstance = fileStreamClass.new_(filePath);
  if (!fileStreamInstance.as<bool>()) {
    return nullptr;
  }
  size_t length = fileStreamInstance.call<val>("length").await().as<size_t>();
  if (length == 0) {
    return nullptr;
  }
  return std::unique_ptr<Stream>(new WebStream(fileStreamInstance, length));
}

WebStream::WebStream(const val& fileStream, size_t length) : length(length), fileStream(fileStream) {}

WebStream::~WebStream() {}

size_t WebStream::size() const {
  return length;
}

bool WebStream::seek(size_t position) {
  if (position >= length) {
    return false;
  }
  currentPosition = position;
  return true;
}

bool WebStream::move(int offset) {
  auto offsetLength = static_cast<size_t>(offset);
  if (currentPosition + offsetLength >= length || currentPosition + offsetLength < 0) {
    return false;
  }
  currentPosition += offsetLength;
  return true;
}

size_t WebStream::read(void* buffer, size_t size) {
  if (currentPosition + size >= length) {
    size = length - currentPosition;
  }
  if (size <= 0) {
    return 0;
  }
  val readPromise = fileStream.call<val>("read", currentPosition, size);
  val emscriptenData = readPromise.await();
  if (emscriptenData.isNull() || emscriptenData.isUndefined()) {
    return 0;
  }
  size_t byteLength = emscriptenData["length"].as<size_t>();
  if (byteLength == 0) {
    return 0;
  }
  auto memory = val::module_property("HEAPU8")["buffer"];
  auto memoryView =
      emscriptenData["constructor"].new_(memory, reinterpret_cast<uintptr_t>(buffer), byteLength);
  memoryView.call<void>("set", emscriptenData);
  currentPosition += byteLength;
  return byteLength;
}

bool WebStream::rewind() {
  currentPosition = 0;
  return true;
}
}  // namespace tgfx