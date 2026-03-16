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

static ClipGeometry ResolveClipGeometry(const ClipElement& a, const ClipElement& b) {
  const auto boundsA = a.bound();
  const auto boundsB = b.bound();
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
    _bound = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    return;
  }

  _bound = path.getBounds();
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
  auto combined = _bound;
  if (!combined.intersect(other._bound)) {
    return false;
  }

  _bound = combined;
  _path = Path();
  _path.addRect(combined);
  // Use the non-pixel-aligned element's AA type.
  if (thisPixelAligned) {
    _antiAlias = other._antiAlias;
  }
  return true;
}

ClipRecord::ClipRecord() : uniqueID(UniqueID::Next()) {
}

ClipData::ClipData() {
  records.push(ClipRecord());
}

ClipStack::ClipStack() : _data(std::make_shared<ClipData>()) {
}

void ClipStack::clip(const Path& path, bool antiAlias) {
  detachIfShared();
  ClipElement toAdd(path, antiAlias);
  addElement(toAdd);
}

void ClipStack::save() {
  detachIfShared();
  auto cur = current();
  cur.startIndex = _data->elements.size();
  _data->records.push(cur);
}

void ClipStack::restore() {
  if (_data->records.size() <= 1) {
    return;
  }

  detachIfShared();
  auto& cur = current();
  auto startIndex = static_cast<int>(cur.startIndex);
  _data->elements.resize(cur.startIndex);
  for (auto& element : _data->elements) {
    if (element.invalidatedByIndex() >= startIndex) {
      element.markInvalid(-1);
    }
  }
  _data->records.pop();
}

void ClipStack::detachIfShared() {
  if (_data.use_count() <= 1) {
    return;
  }
  _data = std::make_shared<ClipData>(*_data);
}

void ClipStack::addElement(const ClipElement& toAdd) {
  auto& cur = current();
  if (cur.state == ClipState::Empty) {
    return;
  }
  if (toAdd.bound().isEmpty()) {
    cur.state = ClipState::Empty;
    return;
  }
  if (!Rect::Intersects(toAdd.bound(), cur.bound)) {
    cur.state = ClipState::Empty;
    return;
  }
  if (toAdd.isRect()) {
    if (toAdd.bound().contains(cur.bound)) {
      return;
    }
  } else {
    if (toAdd.path().contains(cur.bound)) {
      return;
    }
  }
  if (cur.state == ClipState::WideOpen) {
    replaceWithElement(toAdd);
    return;
  }
  appendElement(toAdd);
}

void ClipStack::replaceWithElement(const ClipElement& toAdd) {
  auto& cur = current();
  _data->elements.resize(cur.startIndex);
  _data->elements.push_back(toAdd);
  cur.bound = toAdd.bound();
  cur.state = toAdd.isRect() ? ClipState::Rect : ClipState::Complex;
  cur.oldestValidIndex = _data->elements.size() - 1;
  cur.uniqueID = UniqueID::Next();
}

void ClipStack::appendElement(ClipElement toAdd) {
  auto& cur = current();
  size_t oldestActiveInvalidIdx = _data->elements.size();
  ClipElement* oldestActiveInvalid = nullptr;
  size_t oldestValidIdx = _data->elements.size();
  int64_t youngestValidIdx = static_cast<int64_t>(cur.startIndex) - 1;

  for (size_t i = _data->elements.size(); i > cur.oldestValidIndex;) {
    --i;
    auto& existing = _data->elements[i];
    UpdateElements(existing, toAdd, cur);

    if (!toAdd.isValid()) {
      if (!existing.isValid()) {
        cur.state = ClipState::Empty;
      }
      return;
    } else if (!existing.isValid()) {
      if (i >= cur.startIndex) {
        oldestActiveInvalid = &existing;
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
  if (!oldestActiveInvalid || oldestActiveInvalidIdx >= targetEndIdx) {
    targetEndIdx++;
    oldestActiveInvalid = nullptr;
  }

  // If oldestValidIdx remains unchanged, all existing elements are invalid, so the clip state
  // depends solely on the new element. Must be checked before modifying _data->elements.
  cur.state = (oldestValidIdx == _data->elements.size() && toAdd.isRect()) ? ClipState::Rect
                                                                           : ClipState::Complex;
  _data->elements.resize(targetEndIdx);
  // Reuse the active invalid slot if available, otherwise append to the end.
  if (oldestActiveInvalid) {
    *oldestActiveInvalid = toAdd;
  } else {
    _data->elements.back() = toAdd;
  }

  cur.oldestValidIndex = std::min(oldestValidIdx, oldestActiveInvalidIdx);
  cur.bound.intersect(toAdd.bound());
  cur.uniqueID = UniqueID::Next();
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

  detachIfShared();
  for (auto& element : _data->elements) {
    auto path = element.path();
    path.transform(matrix);
    element = ClipElement(path, element.isAntiAlias());
  }

  // Update the current record after transforming all elements.
  auto& cur = current();
  cur.bound = matrix.mapRect(cur.bound);
  if (cur.state == ClipState::Rect && !matrix.rectStaysRect()) {
    cur.state = ClipState::Complex;
  }
  cur.uniqueID = UniqueID::Next();
}

}  // namespace tgfx
