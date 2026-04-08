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

#include "ClipStack.h"
#include "core/utils/Log.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

/**
 * Describes which clip elements to keep when combining two clip elements.
 */
enum class ClipGeometry {
  // The intersection is empty.
  Empty,
  // B contains A, so Intersect(A, B) = A, B is redundant.
  AOnly,
  // A contains B, so Intersect(A, B) = B, A is redundant.
  BOnly,
  // Both elements are needed.
  Both
};

/**
 * Resolves which clip elements to keep when combining two clip elements. Returns BOnly when A and B
 * are equivalent.
 */
static ClipGeometry ResolveClipGeometry(const ClipElement& a, const ClipElement& b) {
  const auto boundsA = a.bounds();
  const auto boundsB = b.bounds();
  if (!Rect::Intersects(boundsA, boundsB)) {
    return ClipGeometry::Empty;
  }

  if (a.isRect() && b.isRect()) {
    if (boundsB.contains(boundsA)) {
      return ClipGeometry::AOnly;
    }
    if (boundsA.contains(boundsB)) {
      return ClipGeometry::BOnly;
    }
    return ClipGeometry::Both;
  }

  // Complex paths cannot be simplified, keep both.
  if (!a.isRect() && !b.isRect()) {
    if (a.path().isSame(b.path()) && a.isAntiAlias() == b.isAntiAlias()) {
      return ClipGeometry::BOnly;
    }
    return ClipGeometry::Both;
  }

  if (a.isRect()) {
    if (b.path().contains(boundsA)) {
      return ClipGeometry::AOnly;
    }
  } else {
    if (a.path().contains(boundsB)) {
      return ClipGeometry::BOnly;
    }
  }

  return ClipGeometry::Both;
}

static void UpdateElements(ClipElement& existing, ClipElement& toAdd, const ClipRecord& record) {
  DEBUG_ASSERT(toAdd.isValid());
  if (!existing.isValid()) {
    return;
  }

  auto geo = ResolveClipGeometry(existing, toAdd);
  switch (geo) {
    case ClipGeometry::Empty:
      existing.markInvalid(static_cast<int>(record.startIndex));
      toAdd.markInvalid(static_cast<int>(record.startIndex));
      break;
    case ClipGeometry::AOnly:
      toAdd.markInvalid(static_cast<int>(record.startIndex));
      break;
    case ClipGeometry::BOnly:
      existing.markInvalid(static_cast<int>(record.startIndex));
      break;
    case ClipGeometry::Both:
      // Let toAdd absorb existing rather than vice versa, so toAdd can accumulate clip data
      // and potentially optimize with more elements in the stack.
      if (toAdd.tryCombine(existing)) {
        existing.markInvalid(static_cast<int>(record.startIndex));
      }
      break;
  }
}

ClipElement::ClipElement(const Path& path, bool antiAlias) : _path(path), _antiAlias(antiAlias) {
  if (path.isInverseFillType()) {
    _bounds = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    return;
  }

  _bounds = path.getBounds();
  _isRect = path.isRect(nullptr);
}

bool ClipElement::tryCombine(const ClipElement& other) {
  if (!_isRect || !other._isRect) {
    return false;
  }
  // Pixel-aligned rect has sharp edges regardless of AA setting, so we can ignore its AA type.
  auto thisPixelAligned = isPixelAligned();
  auto otherPixelAligned = other.isPixelAligned();
  if (!thisPixelAligned && !otherPixelAligned && _antiAlias != other._antiAlias) {
    return false;
  }
  auto combined = _bounds;
  if (!combined.intersect(other._bounds)) {
    return false;
  }

  _bounds = combined;
  _path = Path();
  _path.addRect(combined);
  // Use the non-pixel-aligned element's AA type.
  if (thisPixelAligned) {
    _antiAlias = other._antiAlias;
  }
  return true;
}

void ClipElement::transform(const Matrix& matrix) {
  _path.transform(matrix);
  if (_path.isInverseFillType()) {
    return;
  }
  _bounds = _path.getBounds();
  _isRect = _path.isRect(nullptr);
}

ClipRecord::ClipRecord() : uniqueID(UniqueID::Next()) {
}

ClipData::ClipData() {
  records.emplace();
}

ClipStack::ClipStack() : _data(std::make_shared<ClipData>()) {
}

void ClipStack::clip(const Path& path, bool antiAlias) {
  bool didSave = willModify();
  ClipElement toAdd(path, antiAlias);
  if (addElement(std::move(toAdd)) || !didSave) {
    return;
  }
  // We made a new save record, but ended up not adding an element to the stack.
  // So instead of keeping an empty save record around, pop it off and restore the counter.
  _data->records.pop();
  current().pushSave();
}

void ClipStack::save() {
  detachIfShared();
  current().pushSave();
}

void ClipStack::restore() {
  detachIfShared();
  auto& cur = current();
  if (cur.hasDeferredSave()) {
    // This was just a deferred save being undone, so the record doesn't need to be removed yet.
    cur.popSave();
    return;
  }

  if (_data->records.size() <= 1) {
    DEBUG_ASSERT(false);  // restore() called without matching save()
    return;
  }

  const auto startIndex = cur.startIndex;
  _data->elements.resize(startIndex);
  for (auto& element : _data->elements) {
    if (element.invalidatedByIndex() >= static_cast<int>(startIndex)) {
      element.markInvalid(-1);
    }
  }

  const auto curTransform = cur.transform;
  _data->records.pop();

  // Restore elements transformed during this save level.
  // The accumulated transform is guaranteed to be invertible (checked in transform()).
  if (curTransform.isIdentity()) {
    return;
  }
  Matrix inverse = {};
  [[maybe_unused]] auto invertible = curTransform.invert(&inverse);
  DEBUG_ASSERT(invertible);
  for (auto& element : _data->elements) {
    element.transform(inverse);
  }
}

void ClipStack::detachIfShared() {
  if (_data.use_count() <= 1) {
    return;
  }
  _data = std::make_shared<ClipData>(*_data);
}

bool ClipStack::addElement(ClipElement&& toAdd) {
  auto& cur = current();
  if (cur.state == ClipState::Empty) {
    // Already empty, adding more clips won't change anything.
    return false;
  }
  if (toAdd.bounds().isEmpty()) {
    cur.state = ClipState::Empty;
    cur.uniqueID = UniqueID::Next();
    return true;
  }
  if (!Rect::Intersects(toAdd.bounds(), cur.bounds)) {
    cur.state = ClipState::Empty;
    cur.uniqueID = UniqueID::Next();
    return true;
  }
  // The new element completely contains current clip, so it's redundant.
  auto curIsContained =
      toAdd.isRect() ? toAdd.bounds().contains(cur.bounds) : toAdd.path().contains(cur.bounds);
  if (curIsContained) {
    return false;
  }
  if (cur.state == ClipState::WideOpen) {
    replaceWithElement(std::move(toAdd));
    return true;
  }
  return appendElement(std::move(toAdd));
}

void ClipStack::replaceWithElement(ClipElement&& toAdd) {
  auto& cur = current();
  _data->elements.resize(cur.startIndex);
  cur.bounds = toAdd.bounds();
  cur.state = toAdd.isRect() ? ClipState::Rect : ClipState::Complex;
  _data->elements.push_back(std::move(toAdd));
  cur.oldestValidIndex = _data->elements.size() - 1;
  cur.uniqueID = UniqueID::Next();
}

bool ClipStack::appendElement(ClipElement&& toAdd) {
  auto& cur = current();
  size_t oldestActiveInvalidIdx = _data->elements.size();
  size_t oldestValidIdx = _data->elements.size();
  int64_t youngestValidIdx = static_cast<int64_t>(cur.startIndex) - 1;

  for (size_t i = _data->elements.size(); i > cur.oldestValidIndex;) {
    --i;
    auto& existing = _data->elements[i];
    UpdateElements(existing, toAdd, cur);

    if (!toAdd.isValid()) {
      if (!existing.isValid()) {
        // Both new and old invalid implies the entire clip becomes empty.
        cur.state = ClipState::Empty;
        return true;
      }
      // The new element doesn't change the clip beyond what the old element already does.
      return false;
    } else if (!existing.isValid()) {
      if (i >= cur.startIndex) {
        oldestActiveInvalidIdx = i;
      }
    } else {
      oldestValidIdx = std::min(oldestValidIdx, i);
      youngestValidIdx = std::max(youngestValidIdx, static_cast<int64_t>(i));
    }
  }

  auto targetEndIdx = static_cast<size_t>(youngestValidIdx + 1);
  // If no active invalid element found, or its index is beyond the youngest valid element,
  // we need to append a new slot after the youngest valid element.
  // "Active" means belonging to the current ClipRecord.
  auto reuseInvalidSlot = oldestActiveInvalidIdx < targetEndIdx;
  if (!reuseInvalidSlot) {
    targetEndIdx++;
  }

  // If oldestValidIdx remains unchanged, all existing elements are invalid, so the clip state
  // depends solely on the new element. Must be checked before modifying _data->elements.
  cur.state = (oldestValidIdx == _data->elements.size() && toAdd.isRect()) ? ClipState::Rect
                                                                           : ClipState::Complex;
  cur.oldestValidIndex = std::min(oldestValidIdx, oldestActiveInvalidIdx);
  cur.bounds.intersect(toAdd.bounds());
  cur.uniqueID = UniqueID::Next();

  _data->elements.resize(targetEndIdx);
  // Reuse the active invalid slot if available, otherwise append to the end.
  if (reuseInvalidSlot) {
    _data->elements[oldestActiveInvalidIdx] = std::move(toAdd);
  } else {
    _data->elements.back() = std::move(toAdd);
  }
  return true;
}

const ClipRecord& ClipStack::current() const {
  DEBUG_ASSERT(!_data->records.empty());
  return _data->records.top();
}

ClipRecord& ClipStack::current() {
  DEBUG_ASSERT(!_data->records.empty());
  return _data->records.top();
}

Path ClipStack::getClipPath() const {
  auto result = Path();
  // Start with infinite bounds.
  result.toggleInverseFillType();
  auto& elems = elements();
  for (size_t i = oldestValidIndex(); i < elems.size(); ++i) {
    const auto& element = elems[i];
    if (element.isValid()) {
      result.addPath(element.path(), PathOp::Intersect);
    }
  }
  return result;
}

void ClipStack::transform(const Matrix& matrix) {
  if (matrix.isIdentity()) {
    return;
  }

  auto newTransform = current().transform;
  newTransform.postConcat(matrix);
  Matrix inverse = {};
  if (!newTransform.invert(&inverse)) {
    DEBUG_ASSERT(false);
    return;
  }

  willModify();
  for (auto& element : _data->elements) {
    element.transform(matrix);
  }

  // Update the current record after transforming all elements.
  auto& cur = current();
  cur.transform = newTransform;
  cur.bounds = matrix.mapRect(cur.bounds);
  if (cur.state == ClipState::Rect && !matrix.rectStaysRect()) {
    cur.state = ClipState::Complex;
  }
  cur.uniqueID = UniqueID::Next();
}

bool ClipStack::willModify() {
  detachIfShared();
  auto& cur = current();
  if (!cur.hasDeferredSave()) {
    return false;
  }

  // Materialize the deferred save.
  cur.popSave();
  auto newRecord = cur;
  newRecord.startIndex = _data->elements.size();
  newRecord.deferredSaveCount = 0;
  _data->records.push(newRecord);
  return true;
}

}  // namespace tgfx
