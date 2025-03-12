////////////////////////////////////////////////////////////////////////////////////////////////
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

#include "SourceContents.h"
#include <vector>
#include "View.h"

SourceContents::SourceContents()
    : files(nullptr), fileStringIdx(0), mdata(nullptr), mdataSize(0), dataBuf(nullptr),
      dataBufSize(0) {
}

SourceContents::~SourceContents() {
  delete[] dataBuf;
}

void SourceContents::Parse(const char* fileName, const tracy::Worker& worker, const View* view) {
  if (files == fileName) return;

  files = fileName;
  fileStringIdx = worker.FindStringIdx(fileName);
  lines.clear();

  if (fileName) {
    size_t sz;
    const auto srcCache = worker.GetSourceFileFromCache(fileName);
    if (srcCache.data != nullptr) {
      mdata = srcCache.data;
      mdataSize = srcCache.len;
      sz = srcCache.len;
    } else {
      FILE* f = fopen(view->sourceSubstitution(fileName), "rb");
      if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
          long pos = ftell(f);
          if (pos < 0) {
            fclose(f);
            files = nullptr;
            return;
          }

          sz = static_cast<size_t>(pos);
          fseek(f, 0, SEEK_SET);
          if (sz > dataBufSize) {
            delete[] dataBuf;
            dataBuf = new char[sz];
            dataBufSize = sz;
          }

          fread(dataBuf, 1, sz, f);
          mdata = dataBuf;
          mdataSize = sz;
          fclose(f);
        } else {
          fclose(f);
          files = nullptr;
          return;
        }
      } else {
        files = nullptr;
      }
    }
  }
}

void SourceContents::Parse(const char* source) {
  const size_t len = strlen(source);

  files = nullptr;
  fileStringIdx = 0;
  mdata = source;
  mdataSize = len;
  Tokenize(source, len);
}

void SourceContents::Tokenize(const char* txt, size_t sz) {
  Tokenizer tokenizer;
  for (;;) {
    auto end = txt;
    auto offset = static_cast<size_t>(end - mdata);
    while (*end != '\n' && *end != '\r' && offset < sz) {
      end++;
      offset = static_cast<size_t>(end - mdata);
    }

    lines.emplace_back(Tokenizer::Line{txt, end, tokenizer.tokenize(txt, end)});
    if (offset == sz) {
      break;
    }
    if (*end == '\n') {
      end++;
      offset = static_cast<size_t>(end - mdata);
      if (offset < sz && *end == '\r') {
        end++;
        offset = static_cast<size_t>(end - mdata);
      }
    } else if (*end == '\r') {
      end++;
      offset = static_cast<size_t>(end - mdata);
      if (offset < sz && *end == '\n') {
        end++;
        offset = static_cast<size_t>(end - mdata);
      }
    }
    if (offset == sz) {
      break;
    }
    txt = end;
  }
}