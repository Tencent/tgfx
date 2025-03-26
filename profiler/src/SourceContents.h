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

#pragma once
#include <vector>
#include "SourceTokenizer.h"
#include "TracyWorker.hpp"

class View;

class SourceContents {
 public:
  SourceContents();
  ~SourceContents();

  void Parse(const char* fileName, const tracy::Worker& worker, const View* view);
  void Parse(const char* source);

  const std::vector<Tokenizer::Line>& get() const {
    return lines;
  }
  bool empty() const {
    return lines.empty();
  }

  const char* filename() const {
    return files;
  }
  uint32_t idx() const {
    return fileStringIdx;
  }
  bool isCached() const {
    return mdata != dataBuf;
  }
  const char* data() const {
    return mdata;
  }
  size_t dataSize() const {
    return mdataSize;
  }

 private:
  void Tokenize(const char* txt, size_t sz);

  const char* files;
  uint32_t fileStringIdx;

  const char* mdata;
  size_t mdataSize;

  char* dataBuf;
  size_t dataBufSize;

  std::vector<Tokenizer::Line> lines;
};
