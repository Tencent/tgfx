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

#pragma once

#include <emscripten/val.h>
#include "tgfx/core/Stream.h"

using namespace emscripten;

namespace tgfx {

class WebStream : public Stream {
 public:
  static std::unique_ptr<Stream> Make(const std::string& filePath);

  ~WebStream() override;

  size_t size() const override;
  bool seek(size_t position) override;
  bool move(int offset) override;
  size_t read(void* buffer, size_t size) override;
  bool rewind() override;

 private:
  WebStream(const val& fileStream, size_t length);
  std::string filePath = "";
  size_t length = 0;
  size_t currentPosition = 0;
  val fileStream;
};

class WebStreamFactory : public StreamFactory {
 public:
  std::unique_ptr<Stream> createStream(const std::string& filePath) override {
    return WebStream::Make(filePath);
  }
};

}  // namespace tgfx
