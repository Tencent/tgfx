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
 * ClipGeometry represents the geometric relationship between two clip elements.
 */
enum class ClipGeometry {
  Empty,  // The intersection is empty.
  AOnly,  // A contains B, so Intersect(A, B) = B, A is redundant.
  BOnly,  // B contains A, so Intersect(A, B) = A, B is redundant.
  Both    // Both elements are needed.
};

/**
 * Resolves the geometric relationship between two clip elements.
 */
static ClipGeometry ResolveClipGeometry(const ClipElement& a, const ClipElement& b) {
  auto boundsA = a.getBound();
  auto boundsB = b.getBound();

  // Bounds don't intersect -> result is empty.
  if (!Rect::Intersects(boundsA, boundsB)) {
    return ClipGeometry::Empty;
  }

  // Both are rectangles -> quick bounds check.
  if (a.getIsRect() && b.getIsRect()) {
    if (boundsA.contains(boundsB)) {
      return ClipGeometry::AOnly;
    }
    if (boundsB.contains(boundsA)) {
      return ClipGeometry::BOnly;
    }
    return ClipGeometry::Both;
  }

  // Both are paths -> cannot determine precisely.
  if (!a.getIsRect() && !b.getIsRect()) {
    return ClipGeometry::Both;
  }

  // One is path, one is rect -> use Path.contains(Rect) to check.
  if (a.getIsRect()) {
    // a is Rect, b is Path
    if (b.getPath().contains(boundsA)) {
      return ClipGeometry::BOnly;  // a is inside b, Intersect = a, b is redundant.
    }
  } else {
    // a is Path, b is Rect
    if (a.getPath().contains(boundsB)) {
      return ClipGeometry::AOnly;  // b is inside a, Intersect = b, a is redundant.
    }
  }

  return ClipGeometry::Both;
}

/**
 * Updates element states based on geometric relationship.
 */
static void UpdateElements(ClipElement& existing, ClipElement& toAdd, const ClipRecord& record) {
  DEBUG_ASSERT(toAdd.isValid());

  // Existing already invalid, doesn't affect toAdd.
  if (!existing.isValid()) {
    return;
  }

  auto geo = ResolveClipGeometry(existing, toAdd);

  if (geo == ClipGeometry::Empty) {
    existing.markInvalid(static_cast<int>(record.startIndex));
    toAdd.markInvalid(static_cast<int>(record.startIndex));
  } else if (geo == ClipGeometry::AOnly) {
    // A contains B, Intersect(A, B) = B, A is redundant.
    existing.markInvalid(static_cast<int>(record.startIndex));
  } else if (geo == ClipGeometry::BOnly) {
    // B contains A, Intersect(A, B) = A, B is redundant.
    toAdd.markInvalid(static_cast<int>(record.startIndex));
  } else if (toAdd.tryCombine(existing)) {
    existing.markInvalid(static_cast<int>(record.startIndex));
  }
}

// ClipElement implementation

ClipElement::ClipElement(const Path& path, bool antiAlias)
    : path(path), antiAlias(antiAlias), bound(path.getBounds()), isRect(path.isRect(nullptr)) {
}

bool ClipElement::tryCombine(const ClipElement& other) {
  // Only combine rectangles with the same AA setting.
  // Path combination requires CPU boolean operations, which is expensive.
  // Keep both elements and defer to GPU mask generation.
  if (!isRect || !other.isRect) {
    return false;
  }
  if (antiAlias != other.antiAlias) {
    return false;
  }

  // Combine as rectangle intersection.
  auto combined = bound;
  if (!combined.intersect(other.bound)) {
    return false;
  }

  bound = combined;
  path = Path();
  path.addRect(combined);
  return true;
}

// ClipRecord implementation

ClipRecord::ClipRecord() : uniqueID(UniqueID::Next()) {
}

ClipData::ClipData() {
  // Initialize with a default ClipRecord (WideOpen state).
  records.push(ClipRecord());
}

// ClipStack implementation

ClipStack::ClipStack() : _data(std::make_shared<ClipData>()) {
}

void ClipStack::clip(const Path& path, bool antiAlias) {
  detachIfShared();
  ClipElement toAdd(path, antiAlias);
  addElement(toAdd);
}

void ClipStack::save() {
  detachIfShared();
  // Copy current state.
  auto cur = current();
  // New level starts from current element end.
  cur.startIndex = _data->elements.size();
  _data->records.push(cur);
}

void ClipStack::restore() {
  if (_data->records.size() <= 1) {
    // Cannot restore past the initial state.
    return;
  }

  detachIfShared();
  auto& cur = current();
  auto startIndex = static_cast<int>(cur.startIndex);
  // Pop Active elements first.
  _data->elements.resize(cur.startIndex);
  // Restore Non-Active elements that were invalidated by the current level.
  for (auto& element : _data->elements) {
    if (element.getInvalidatedByIndex() >= startIndex) {
      element.markInvalid(-1);
    }
  }
  // Pop the current record (uniqueID is automatically restored to the previous state).
  _data->records.pop();
}

void ClipStack::reset(const ClipRecord& record, const std::vector<ClipElement>& elements) {
  detachIfShared();
  std::stack<ClipRecord>().swap(_data->records);
  _data->records.push(record);
  _data->elements = elements;
}

void ClipStack::detachIfShared() {
  if (_data.use_count() > 1) {
    _data = std::make_shared<ClipData>(*_data);
  }
}

void ClipStack::addElement(const ClipElement& toAdd) {
  auto& cur = current();

  // Quick check: already empty, no need to continue.
  if (cur.state == ClipState::Empty) {
    return;
  }

  // Empty element -> entire clip becomes empty.
  if (toAdd.getBound().isEmpty()) {
    cur.state = ClipState::Empty;
    return;
  }

  // First layer: quick comparison with overall bounds.
  if (!Rect::Intersects(toAdd.getBound(), cur.bound)) {
    cur.state = ClipState::Empty;
    return;
  }
  if (toAdd.getBound().contains(cur.bound) && toAdd.getIsRect()) {
    return;  // toAdd completely contains current clip, redundant.
  }

  // WideOpen special handling: initialize directly with new element.
  if (cur.state == ClipState::WideOpen) {
    replaceWithElement(toAdd);
    return;
  }

  // Second layer: compare with each element.
  appendElement(toAdd);
}

void ClipStack::replaceWithElement(const ClipElement& toAdd) {
  auto& cur = current();

  // Clear Active elements of current ClipRecord.
  _data->elements.resize(cur.startIndex);

  // Add new element.
  _data->elements.push_back(toAdd);
  cur.bound = toAdd.getBound();
  cur.state = toAdd.getIsRect() ? ClipState::Rect : ClipState::Complex;
  cur.oldestValidIndex = _data->elements.size() - 1;
  cur.uniqueID = UniqueID::Next();
}

void ClipStack::appendElement(ClipElement toAdd) {
  auto& cur = current();
  // Reusable slot index.
  size_t oldestInvalidIdx = _data->elements.size();
  // Reusable slot pointer.
  ClipElement* oldestInvalid = nullptr;
  // Oldest valid index.
  size_t oldestValidIdx = _data->elements.size();
  // Youngest valid index, use signed type to avoid underflow.
  // Initial value is startIndex - 1, when no valid element found, targetEndIdx = startIndex.
  int64_t youngestValidIdx = static_cast<int64_t>(cur.startIndex) - 1;

  // Iterate from newest to oldest [oldestValidIndex, count-1].
  for (size_t i = _data->elements.size(); i > cur.oldestValidIndex;) {
    --i;
    auto& existing = _data->elements[i];

    UpdateElements(existing, toAdd, cur);

    if (!toAdd.isValid()) {
      if (!existing.isValid()) {
        // Both invalid, clip becomes empty.
        cur.state = ClipState::Empty;
        return;
      } else {
        // toAdd is redundant, discard.
        return;
      }
    } else if (!existing.isValid()) {
      // existing is eliminated.
      if (i >= cur.startIndex) {
        // Active element, record reusable slot.
        oldestInvalid = &existing;
        oldestInvalidIdx = i;
      }
    } else {
      // Both valid.
      oldestValidIdx = std::min(oldestValidIdx, i);
      youngestValidIdx = std::max(youngestValidIdx, static_cast<int64_t>(i));
    }
  }

  // Calculate target end index.
  auto targetEndIdx = static_cast<size_t>(youngestValidIdx + 1);
  if (!oldestInvalid || oldestInvalidIdx >= targetEndIdx) {
    // toAdd needs new slot (append to end).
    targetEndIdx++;
    oldestInvalid = nullptr;
  }

  // Update ClipState (must be before resize, size is still original value).
  cur.state = (oldestValidIdx == _data->elements.size() && toAdd.getIsRect()) ? ClipState::Rect
                                                                              : ClipState::Complex;

  // Clean up trailing invalid elements.
  _data->elements.resize(targetEndIdx);

  // Store toAdd.
  if (oldestInvalid) {
    *oldestInvalid = toAdd;
  } else {
    // resize already allocated space, assign directly.
    _data->elements.back() = toAdd;
  }

  // Update other states.
  cur.oldestValidIndex = std::min(oldestValidIdx, oldestInvalidIdx);
  cur.bound.intersect(toAdd.getBound());
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

}  // namespace tgfx
