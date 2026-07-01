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
#include <type_traits>
#include <vector>
#include "core/GeometryShape.h"
#include "core/utils/GeometryExtra.h"
#include "core/utils/Log.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
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
  ClipElement();
  ClipElement(const GeometryShape& shape, const Matrix& matrix, bool antiAlias);

  const GeometryShape& shape() const {
    return _shape;
  }

  const Matrix& matrix() const {
    return _matrix;
  }

  bool antiAlias() const {
    return _antiAlias;
  }

  const Rect& outerBounds() const {
    return _outerBounds;
  }

  const Rect& innerBounds() const {
    return _innerBounds;
  }

  // Maps the element's geometry to the clip state it produces on its own: an empty shape yields
  // Empty, a device-space rect yields Rect, and every other shape yields Complex.
  ClipState clipState() const;

  bool isValid() const {
    return _invalidatedByIndex < 0;
  }

  /**
   * Returns a device-space Path representing this element's shape. The shape is converted to a path
   * in its local space and transformed by the element's matrix.
   */
  Path getDevicePath() const;

  bool tryCombine(const ClipElement& other);

  /**
   * Returns whether this element's keep-region fully contains the keep-region of other.
   *
   * The check is conservative: a true result guarantees containment, but a false result only
   * means containment could not be cheaply proven and may or may not actually hold. For example,
   * when this is a concave path such as an L shape and other's bounds fit entirely inside the L,
   * the geometric check cannot prove containment and returns false even though it does hold.
   */
  bool contains(const ClipElement& other) const;

  void transform(const Matrix& matrix);

  void markInvalid(int byIndex) {
    _invalidatedByIndex = byIndex;
  }

  int invalidatedByIndex() const {
    return _invalidatedByIndex;
  }

 private:
  // Reduces the shape to its simplest type, folds an axis-aligned matrix into the geometry, then
  // refreshes the bounds and drops the element to empty when it covers no pixels. Call after any
  // change to _shape or _matrix.
  void simplify();
  // Recomputes the outer/inner bounds from the current shape and matrix.
  void updateOuterInnerBounds();

  GeometryShape _shape = {};
  Matrix _matrix = Matrix::I();
  Rect _outerBounds = {};
  Rect _innerBounds = {};
  bool _antiAlias = false;
  // The startIndex of the ClipRecord that invalidated this element. -1 means valid.
  int _invalidatedByIndex = -1;
};

struct ClipRecord {
  // First element index owned by this save level.
  size_t startIndex = 0;
  // Oldest valid element index globally.
  size_t oldestValidIndex = 0;
  // Infinite bounds for WideOpen state, actual clip bounds otherwise.
  Rect bounds = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
  ClipState state = ClipState::WideOpen;
  uint32_t uniqueID = 0;
  // Number of save() calls without modifications (yet). When > 0, this record has deferred saves.
  int deferredSaveCount = 0;
  // Accumulated transform applied during this save level. Used to restore elements on pop.
  Matrix transform = Matrix::I();

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
    DEBUG_ASSERT(deferredSaveCount > 0);
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

  // Adds a rectangle clip with the given matrix mapping it from local space to device space.
  void clipRect(const Rect& rect, const Matrix& matrix, bool antiAlias);

  // Adds a rounded rectangle clip with the given matrix mapping it from local space to
  // device space.
  void clipRRect(const RRect& rRect, const Matrix& matrix, bool antiAlias);

  // Adds a generic path clip with the given matrix mapping it from local space to device space.
  void clipPath(const Path& path, const Matrix& matrix, bool antiAlias);

  void save();

  void restore();

  ClipState state() const {
    return current().state;
  }

  const Rect& bounds() const {
    return current().bounds;
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
  // Shared implementation of clipRect/clipRRect/clipPath: wraps the shape in a ClipElement and adds
  // it to the stack, cleaning up an empty save record if the element turned out to be redundant.
  void clipShape(GeometryShape&& shape, const Matrix& matrix, bool antiAlias);

  /**
   * Adds a clip element to the stack.
   * @return true if the element was actually added or affected the clip state, false if it was
   * redundant.
   */
  bool addElement(ClipElement&& toAdd);
  void replaceWithElement(ClipElement&& toAdd);
  /**
   * Appends a clip element, updating existing elements as needed.
   * @return true if the element was added or affected the clip state, false if it was redundant.
   */
  bool appendElement(ClipElement&& toAdd);
  void detachIfShared();
  const ClipRecord& current() const;
  ClipRecord& current();
  /**
   * Prepares the clip stack for modification. Detaches shared data and materializes
   * any deferred save if needed.
   * @return true if a deferred save was materialized, false otherwise.
   */
  bool willModify();

  std::shared_ptr<ClipData> _data = nullptr;
};

}  // namespace tgfx
