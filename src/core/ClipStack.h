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

#include <cfloat>
#include <cmath>
#include <memory>
#include <stack>
#include <vector>
#include "core/utils/GeometryExtra.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * Represents the current state of the clip region.
 */
enum class ClipState {
  // The clip region is empty, nothing will be drawn.
  Empty,
  // No clip has been applied, everything is visible.
  WideOpen,
  // The clip is a single rectangle.
  Rect,
  // The clip involves non-rectangular paths or multiple elements.
  Complex
};

class ClipElement {
 public:
  ClipElement() = default;
  ClipElement(const Path& path, bool antiAlias);

  const Path& path() const {
    return _path;
  }

  bool isAntiAlias() const {
    return _antiAlias;
  }

  const Rect& bound() const {
    return _bound;
  }

  bool isRect() const {
    return _isRect;
  }

  bool isValid() const {
    return _invalidatedByIndex < 0;
  }

  bool isPixelAligned() const {
    return IsPixelAligned(_bound.left) && IsPixelAligned(_bound.top) &&
           IsPixelAligned(_bound.right) && IsPixelAligned(_bound.bottom);
  }

  bool tryCombine(const ClipElement& other);

  void markInvalid(int byIndex) {
    _invalidatedByIndex = byIndex;
  }

  int invalidatedByIndex() const {
    return _invalidatedByIndex;
  }

 private:
  Path _path = {};
  bool _antiAlias = false;
  Rect _bound = {};
  bool _isRect = false;
  // The startIndex of the ClipRecord that invalidated this element. -1 means valid.
  int _invalidatedByIndex = -1;
};

struct ClipRecord {
  // First element index owned by this save level.
  size_t startIndex = 0;
  // Oldest valid element index globally.
  size_t oldestValidIndex = 0;
  // Infinite bounds for WideOpen state, actual clip bounds otherwise.
  Rect bound = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
  ClipState state = ClipState::WideOpen;
  uint32_t uniqueID = 0;
  // Number of save() calls without modifications (yet). When > 0, this record has deferred saves.
  int deferredSaveCount = 0;

  ClipRecord();

  /**
   * Returns true if there are deferred saves pending.
   */
  bool hasDeferredSave() const {
    return deferredSaveCount > 0;
  }

  /**
   * Increments the deferred save count.
   */
  void pushSave() {
    deferredSaveCount++;
  }

  /**
   * Decrements the deferred save count.
   */
  void popSave() {
    deferredSaveCount--;
  }
};

struct ClipData {
  std::vector<ClipElement> elements = {};
  std::stack<ClipRecord> records = {};

  ClipData();
};

/**
 * ClipStack manages clip state with element merging and save/restore logic.
 * Uses Copy-on-Write for efficient copying.
 */
class ClipStack {
 public:
  ClipStack();

  void clip(const Path& path, bool antiAlias);

  void save();

  void restore();

  ClipState state() const {
    return current().state;
  }

  const Rect& bound() const {
    return current().bound;
  }

  const std::vector<ClipElement>& elements() const {
    return _data->elements;
  }

  size_t oldestValidIndex() const {
    return current().oldestValidIndex;
  }

  uint32_t uniqueID() const {
    return current().uniqueID;
  }

  bool isSame(const ClipStack& other) const {
    return uniqueID() == other.uniqueID();
  }

  /**
   * Returns the combined clip path by intersecting all valid clip elements.
   * @note The anti-aliasing information of each clip element is lost in the returned path.
   */
  Path getClipPath() const;

  /**
   * Transforms all clip element paths by the given matrix.
   */
  void transform(const Matrix& matrix);

 private:
  /**
   * Adds a clip element to the stack.
   * @return true if the element was actually added or affected the clip state, false if it was
   * redundant.
   */
  bool addElement(const ClipElement& toAdd);
  void replaceWithElement(const ClipElement& toAdd);
  /**
   * Appends a clip element, updating existing elements as needed.
   * @return true if the element was added or affected the clip state, false if it was redundant.
   */
  bool appendElement(ClipElement toAdd);
  void detachIfShared();
  const ClipRecord& current() const;
  ClipRecord& current();
  /**
   * Prepares the clip stack for modification. Detaches shared data and materializes
   * any deferred save if needed.
   * @return true if a deferred save was materialized, false otherwise.
   */
  bool willModify();

  std::shared_ptr<ClipData> _data;
};

}  // namespace tgfx
