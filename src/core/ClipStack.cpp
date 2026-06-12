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
#include "RRectUtils.h"
#include "core/utils/Log.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

static bool IsPixelAligned(const Rect& rect) {
  return IsPixelAligned(rect.left) && IsPixelAligned(rect.top) && IsPixelAligned(rect.right) &&
         IsPixelAligned(rect.bottom);
}

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
  // Check disjointness. For normal elements, outerBounds intersection suffices. For
  // inverse-fill elements, the keep-region is the complement of the shape, so disjointness
  // occurs when the other element's bounds lie entirely within the inverse shape.
  auto isInverse = [](const ClipElement& e) {
    return e.type() == ClipElement::Type::Path && e.asPath().isInverseFillType();
  };
  const bool aInverse = isInverse(a);
  const bool bInverse = isInverse(b);
  if (!aInverse && !bInverse) {
    if (!Rect::Intersects(a.outerBounds(), b.outerBounds())) {
      return ClipGeometry::Empty;
    }
  } else if (aInverse && bInverse) {
    // Two inverse-fill regions always overlap (both are near-full-plane).
  } else {
    // One inverse, one normal. They are disjoint when the normal element's outerBounds lies
    // entirely inside the inverse element's removed shape.
    const auto& inv = aInverse ? a : b;
    const auto& normal = aInverse ? b : a;
    auto invPath = inv.asPath();
    // invPath is inverse-fill; its getBounds() returns the shape bounds. If the shape contains
    // the normal element's outerBounds, the normal element is entirely in the removed region.
    invPath.toggleInverseFillType();
    if (invPath.contains(normal.outerBounds())) {
      return ClipGeometry::Empty;
    }
  }

  if (b.contains(a)) {
    return ClipGeometry::AOnly;
  }
  if (a.contains(b)) {
    return ClipGeometry::BOnly;
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
      if (toAdd.tryCombine(existing)) {
        existing.markInvalid(static_cast<int>(record.startIndex));
        if (toAdd.type() == ClipElement::Type::Empty) {
          toAdd.markInvalid(static_cast<int>(record.startIndex));
        }
      }
      break;
  }
}

ClipElement::ClipElement() : _rect(Rect::MakeEmpty()), _type(Type::Rect) {
}

bool ClipElement::isPixelAligned() const {
  DEBUG_ASSERT(_type == Type::Rect && _matrix.isIdentity());
  return IsPixelAligned(_rect);
}

ClipElement::ClipElement(const Rect& rect, const Matrix& matrix, bool antiAlias)
    : _rect(rect), _type(Type::Rect), _matrix(matrix), _antiAlias(antiAlias) {
  simplify();
  updateOuterInnerBounds();
}

ClipElement::ClipElement(const RRect& rRect, const Matrix& matrix, bool antiAlias)
    : _rRect(rRect), _type(Type::RRect), _matrix(matrix), _antiAlias(antiAlias) {
  simplify();
  updateOuterInnerBounds();
}

ClipElement::ClipElement(const Path& path, bool antiAlias)
    : _rect(Rect::MakeEmpty()), _type(Type::Rect), _antiAlias(antiAlias) {
  setPath(path);
  updateOuterInnerBounds();
}

ClipElement::ClipElement(const ClipElement& other)
    : _rect(Rect::MakeEmpty()), _type(Type::Rect), _matrix(other._matrix),
      _outerBounds(other._outerBounds), _innerBounds(other._innerBounds),
      _antiAlias(other._antiAlias), _invalidatedByIndex(other._invalidatedByIndex) {
  switch (other._type) {
    case Type::Empty:
      break;
    case Type::Rect:
      setRect(other._rect);
      break;
    case Type::RRect:
      setRRect(other._rRect);
      break;
    case Type::Path:
      setPath(other._path);
      break;
  }
}

ClipElement& ClipElement::operator=(const ClipElement& other) {
  if (this == &other) {
    return *this;
  }
  switch (other._type) {
    case Type::Empty:
      setType(Type::Empty);
      break;
    case Type::Rect:
      setRect(other._rect);
      break;
    case Type::RRect:
      setRRect(other._rRect);
      break;
    case Type::Path:
      setPath(other._path);
      break;
  }
  _matrix = other._matrix;
  _outerBounds = other._outerBounds;
  _innerBounds = other._innerBounds;
  _antiAlias = other._antiAlias;
  _invalidatedByIndex = other._invalidatedByIndex;
  return *this;
}

ClipElement::~ClipElement() {
  setType(Type::Empty);
}

void ClipElement::setType(Type type) {
  if (_type == Type::Path && type != Type::Path) {
    _path.~Path();
  }
  _type = type;
}

void ClipElement::setRect(const Rect& rect) {
  setType(Type::Empty);
  new (&_rect) Rect(rect);
  _type = Type::Rect;
}

void ClipElement::setRRect(const RRect& rRect) {
  setType(Type::Empty);
  new (&_rRect) RRect(rRect);
  _type = Type::RRect;
}

void ClipElement::setPath(const Path& path) {
  if (_type == Type::Path) {
    _path = path;
  } else {
    _type = Type::Path;
    new (&_path) Path(path);
  }
}

Rect ClipElement::getBounds() const {
  switch (_type) {
    case Type::Empty:
      return Rect::MakeEmpty();
    case Type::Rect:
      return _rect;
    case Type::RRect:
      return _rRect.rect();
    case Type::Path:
      return _path.getBounds();
  }
}

void ClipElement::updateOuterInnerBounds() {
  if (_type == Type::Empty) {
    _outerBounds = Rect::MakeEmpty();
    _innerBounds = Rect::MakeEmpty();
    return;
  }

  // Path type always has identity matrix because transform() bakes the matrix into the path
  // directly, so its bounds are already in device space.
  Rect outer = getBounds();
  if (_type != Type::Path) {
    outer = _matrix.mapRect(outer);
  }

  if (_type == Type::Path && _path.isInverseFillType()) {
    _outerBounds = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    _innerBounds = Rect::MakeEmpty();
    return;
  }

  _outerBounds = outer;
  _outerBounds.roundOut();

  if (_type == Type::Rect && _matrix.isIdentity()) {
    // Axis-aligned device-space rect: inner bounds equal the exact mapped rect.
    _innerBounds = outer;
  } else if (_type == Type::RRect && _matrix.isIdentity()) {
    // Identity-matrix RRect: compute the largest inscribed rectangle that avoids the corners.
    _innerBounds = RRectUtils::InnerBounds(_rRect);
    _innerBounds.roundIn();
  } else {
    // For non-identity matrices the mapped bounding box is not a reliable inner bound (the actual
    // shape may not fill the entire box), and for Path types the inner region cannot be computed
    // cheaply. Fall back to empty.
    _innerBounds = Rect::MakeEmpty();
  }
}

void ClipElement::simplify() {
  if (_type == Type::Path || _type == Type::Empty) {
    return;
  }
  if (!_matrix.rectStaysRect()) {
    return;
  }
  if (_type == Type::Rect) {
    _rect = _matrix.mapRect(_rect);
    _matrix.setIdentity();
    return;
  }
  auto transformed = RRectUtils::TryAxisAlignedTransform(_rRect, _matrix);
  if (!transformed.has_value()) {
    return;
  }
  _matrix.setIdentity();
  if (transformed->type() == RRect::Type::Rect) {
    setRect(transformed->rect());
  } else {
    _rRect = *transformed;
  }
}

Path ClipElement::asPath() const {
  Path path = {};
  switch (_type) {
    case Type::Empty:
      return path;
    case Type::Rect:
      path.addRect(_rect);
      break;
    case Type::RRect:
      path.addRRect(_rRect);
      break;
    case Type::Path:
      return _path;
  }
  if (!_matrix.isIdentity()) {
    path.transform(_matrix);
  }
  return path;
}

bool ClipElement::contains(const ClipElement& other) const {
  if (_type == Type::Empty || other._type == Type::Empty) {
    return false;
  }
  if (_innerBounds.contains(other._outerBounds)) {
    return true;
  }

  if (_type == Type::Path && _path.isInverseFillType()) {
    if (other._type == Type::Path && other._path.isInverseFillType()) {
      return other._path.contains(_path.getBounds());
    }
    return !Rect::Intersects(_path.getBounds(), other._outerBounds);
  }
  if (other._type == Type::Path && other._path.isInverseFillType()) {
    return false;
  }

  if (_antiAlias == other._antiAlias && _matrix == other._matrix) {
    if (_type != Type::Path && other._type != Type::Path) {
      const auto thisRR = _type == Type::Rect ? RRect::MakeRect(_rect) : _rRect;
      const auto otherRR = other._type == Type::Rect ? RRect::MakeRect(other._rect) : other._rRect;
      const auto intersected = RRectUtils::ConservativeIntersect(thisRR, otherRR);
      if (intersected.has_value()) {
        return intersected->rect() == otherRR.rect() && intersected->radii() == otherRR.radii();
      }
    }
    if (_type == Type::Path && other._type == Type::Path) {
      return _path.isSame(other._path);
    }
  }

  if (_type == Type::Rect && _matrix.isIdentity()) {
    return _outerBounds.contains(other._outerBounds);
  }
  return false;
}

bool ClipElement::tryCombine(const ClipElement& other) {
  // Path/Empty elements cannot be simplified. Different matrices are incompatible.
  if (_type == Type::Path || _type == Type::Empty || other._type == Type::Path ||
      other._type == Type::Empty || _matrix != other._matrix) {
    return false;
  }

  // Case 1: Rect + Rect.
  if (_type == Type::Rect && other._type == Type::Rect) {
    if (_antiAlias != other._antiAlias) {
      if (!_matrix.isIdentity()) {
        // Non-identity matrix means the rect may not be axis-aligned in device space, so the
        // pixel-aligned check is not meaningful. Reject AA mismatch in this case.
        return false;
      }

      if (IsPixelAligned(_rect)) {
        // Case 1a: this is pixel-aligned so AA has no visual effect, adopt other's AA flag.
        _antiAlias = other._antiAlias;
      } else if (!IsPixelAligned(other._rect)) {
        // Case 1b: neither side is pixel-aligned and AA flags differ, cannot merge.
        return false;
      }
      // Case 1c: other is pixel-aligned, its AA flag is irrelevant, keep this->_antiAlias.
    }

    if (!_rect.intersect(other._rect)) {
      setType(Type::Empty);
      return true;
    }
  } else {
    // Case 2: Rect + RRect / RRect + RRect.
    DEBUG_ASSERT(_type == Type::Rect || _type == Type::RRect);
    DEBUG_ASSERT(other._type == Type::Rect || other._type == Type::RRect);
    // RRect has curved arcs at corners where AA always affects visual output, so unlike Rect there
    // is no pixel-aligned exemption. AA flags must match exactly.
    if (_antiAlias != other._antiAlias) {
      return false;
    }

    const auto thisRRect = _type == Type::Rect ? RRect::MakeRect(_rect) : _rRect;
    const auto otherRRect = other._type == Type::Rect ? RRect::MakeRect(other._rect) : other._rRect;
    const auto mergedRRect = RRectUtils::ConservativeIntersect(thisRRect, otherRRect);
    if (!mergedRRect.has_value()) {
      // The intersection cannot be represented as a single RRect (e.g., incompatible corner radii
      // or radii overflow), so we cannot merge the two elements.
      return false;
    }

    if (mergedRRect->rect().isEmpty()) {
      setType(Type::Empty);
      return true;
    }

    if (mergedRRect->type() == RRect::Type::Rect) {
      setRect(mergedRRect->rect());
    } else {
      setRRect(*mergedRRect);
    }
  }

  // The combined bounds can be computed by intersecting the existing bounds directly, avoiding
  // the full recomputation in updateOuterInnerBounds(). The outerBounds intersection cannot fail
  // because empty intersections are already handled above.
  DEBUG_ASSERT_RESULT(_outerBounds.intersect(other._outerBounds));
  if (!_innerBounds.intersect(other._innerBounds)) {
    // This can happen when an RRect with asymmetric radii causes InnerBounds to pick a narrow
    // strip strategy that does not cover the other element's inner region. E.g., RRect(LTRB)
    // [0,0,300,200] with radii TL(10,90) TR(10,90) BR(20,90) BL(10,90) and Rect [280,0,300,100].
    _innerBounds = Rect::MakeEmpty();
  }
  return true;
}

void ClipElement::transform(const Matrix& matrix) {
  if (_type == Type::Path) {
    _path.transform(matrix);
    updateOuterInnerBounds();
    return;
  }
  _matrix.postConcat(matrix);
  simplify();
  updateOuterInnerBounds();
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

void ClipStack::clipRect(const Rect& rect, const Matrix& matrix, bool antiAlias) {
  bool didSave = willModify();
  ClipElement toAdd(rect, matrix, antiAlias);
  if (addElement(std::move(toAdd)) || !didSave) {
    return;
  }
  _data->records.pop();
  current().pushSave();
}

void ClipStack::clipRRect(const RRect& rRect, const Matrix& matrix, bool antiAlias) {
  bool didSave = willModify();
  ClipElement toAdd(rRect, matrix, antiAlias);
  if (addElement(std::move(toAdd)) || !didSave) {
    return;
  }
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
  if (toAdd.outerBounds().isEmpty()) {
    cur.state = ClipState::Empty;
    cur.uniqueID = UniqueID::Next();
    return true;
  }
  if (cur.state == ClipState::WideOpen) {
    replaceWithElement(std::move(toAdd));
    return true;
  }
  // Wrap cur.bounds as a non-inverse rect ClipElement so ResolveClipGeometry
  // can handle containment and intersection against toAdd uniformly.
  Path curBoundsPath = {};
  curBoundsPath.addRect(cur.bounds);
  const ClipElement curElement(curBoundsPath, false);
  switch (ResolveClipGeometry(curElement, toAdd)) {
    case ClipGeometry::Empty:
      cur.state = ClipState::Empty;
      cur.uniqueID = UniqueID::Next();
      return true;
    case ClipGeometry::AOnly:
      // toAdd's keep-region already covers cur.bounds entirely, so toAdd is redundant.
      return false;
    case ClipGeometry::BOnly:
      // cur.bounds is only an optimistic envelope of existing elements: some area
      // inside it may have been carved out and not actually kept. Existing elements
      // cannot be dropped here. Fall through to per-element handling.
    case ClipGeometry::Both:
      return appendElement(std::move(toAdd));
  }
  return false;
}

void ClipStack::replaceWithElement(ClipElement&& toAdd) {
  auto& cur = current();
  _data->elements.resize(cur.startIndex);
  cur.bounds = toAdd.outerBounds();
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
  cur.bounds.intersect(toAdd.outerBounds());
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
      result.addPath(element.asPath(), PathOp::Intersect);
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
