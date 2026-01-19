/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "core/GlyphRun.h"

namespace tgfx {

class TextBlob;
struct RunRecord;

// A lightweight list class for iterating over GlyphRuns without allocating a vector.
class GlyphRunList {
 public:
  class Iterator {
   public:
    Iterator(const RunRecord* record, size_t remaining) : current(record), remaining(remaining) {
    }

    GlyphRun operator*() const;

    Iterator& operator++();

    bool operator!=(const Iterator& other) const {
      return remaining != other.remaining;
    }

   private:
    const RunRecord* current = nullptr;
    size_t remaining = 0;
  };

  explicit GlyphRunList(const TextBlob* blob) : blob(blob) {
  }

  Iterator begin() const;

  Iterator end() const {
    return Iterator(nullptr, 0);
  }

  bool empty() const;

 private:
  const TextBlob* blob = nullptr;
};

}  // namespace tgfx
