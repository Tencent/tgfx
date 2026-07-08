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
#include <algorithm>
#include "RRectUtils.h"
#include "core/utils/Log.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

using ShapeType = GeometryShape::Type;

// A non-AA coordinate whose fractional part is near 0.5 sits between two pixels, and rasterization
// may count that pixel as covered or not. Offsetting the coordinate away from the halfway point
// before rounding biases the resulting bound to the safe side, so the ambiguous pixel is
// deliberately included or excluded, with actual coverage still decided by the GPU. AA uses
// floor/ceil and needs no offset.
static constexpr float HALF_PIXEL_ROUNDING_TOLERANCE = 5e-2f;

// A mapped point's w' is 0 on the camera plane, negative behind it, and a tiny positive value just
// in front of it makes the divided coordinates blow up. A w' below this small positive threshold
// therefore covers all the cases where perspective division no longer yields a usable position.
static constexpr float W0_PLANE_DISTANCE = 1.f / (1 << 14);

// Rounds a coordinate down to the pixel grid (floor-like), moving it in the negative direction.
static inline float RoundPixelLow(float v, bool antiAlias) {
  v += CLIP_BOUNDS_TOLERANCE;
  return antiAlias ? floorf(v) : roundf(v - HALF_PIXEL_ROUNDING_TOLERANCE);
}

// Rounds a coordinate up to the pixel grid (ceil-like), moving it in the positive direction.
static inline float RoundPixelHigh(float v, bool antiAlias) {
  v -= CLIP_BOUNDS_TOLERANCE;
  return antiAlias ? ceilf(v) : roundf(v + HALF_PIXEL_ROUNDING_TOLERANCE);
}

// Converts analytic shape bounds to an integer pixel box for the given AA mode.
// exterior: tight cover of pixels that may get non-zero coverage.
// interior: largest set of pixels guaranteed fully covered.
static Rect GetPixelBounds(const Rect& bounds, bool antiAlias, bool exterior) {
  if (bounds.isEmpty()) {
    return Rect::MakeEmpty();
  }
  if (exterior) {
    return Rect::MakeLTRB(
        RoundPixelLow(bounds.left, antiAlias), RoundPixelLow(bounds.top, antiAlias),
        RoundPixelHigh(bounds.right, antiAlias), RoundPixelHigh(bounds.bottom, antiAlias));
  }
  return Rect::MakeLTRB(
      RoundPixelHigh(bounds.left, antiAlias), RoundPixelHigh(bounds.top, antiAlias),
      RoundPixelLow(bounds.right, antiAlias), RoundPixelLow(bounds.bottom, antiAlias));
}

// Conservative containment test: returns true only when the shape provably contains the rect.
// A false result may just mean containment could not be decided cheaply, not that it fails.
static bool ShapeContainsRect(const GeometryShape& shape, const Matrix& shapeToDevice,
                              const Rect& rect, const Matrix& rectToDevice, bool mixedAA) {
  if (!shape.convex()) {
    // Containment against a concave shape has no cheap test.
    return false;
  }

  Matrix deviceToShape = {};
  if (!shapeToDevice.invert(&deviceToShape)) {
    // Clip element matrices are always invertible, so this should never happen.
    DEBUG_ASSERT(false);
    return false;
  }

  if (!mixedAA && shapeToDevice == rectToDevice) {
    // Both live in the same coordinate space, so compare their shapes directly without mapping.
    return shape.conservativeContains(rect);
  }
  if (rectToDevice.isIdentity() && shapeToDevice.rectStaysRect()) {
    // The rect is already in device space and shapeToDevice preserves the rect shape, so mapping
    // the rect into the shape's space keeps it an axis-aligned rect and containment is a direct
    // rect test.
    auto rectInShape = rect;
    if (mixedAA) {
      rectInShape.outset(0.5f, 0.5f);
    }
    deviceToShape.mapRect(&rectInShape);
    return shape.conservativeContains(rectInShape);
  }

  if (mixedAA) {
    // Under mixedAA the rect needs a half-pixel expansion, which must offset each mapped quad edge
    // along its normal and cannot be done by a simple outset here, so conservatively report
    // "not contained".
    // TODO: Reuse core-level per-edge quad outsetting (as in AAQuadsVertexProvider) for this test.
    return false;
  }

  // The shape is convex here, so it contains the rect once it contains all four corners.
  const Point localCorners[4] = {{rect.left, rect.top},
                                 {rect.right, rect.top},
                                 {rect.right, rect.bottom},
                                 {rect.left, rect.bottom}};
  for (const auto& localCorner : localCorners) {
    const auto h = rectToDevice.mapHomogeneous(localCorner.x, localCorner.y, 1.0f);
    if (h.z < W0_PLANE_DISTANCE) {
      // h.z carries the mapped w; below the threshold perspective division no longer gives a
      // dependable device position, so the shape cannot reliably contain the rect.
      return false;
    }
    const auto invW = 1.0f / h.z;
    // This is a conservative containment test, so the w of deviceToShape's mapping is not
    // re-checked when mapping the corner back into the shape's space.
    if (!shape.contains(deviceToShape.mapXY(h.x * invW, h.y * invW))) {
      return false;
    }
  }
  return true;
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
  // A mixed normal/inverse pair is disjoint only when the normal element lies entirely inside the
  // inverse-fill path's removed region.
  const bool aInverse = a.shape().isPath() && a.shape().path().isInverseFillType();
  const bool bInverse = b.shape().isPath() && b.shape().path().isInverseFillType();
  if (aInverse != bInverse) {
    const auto& inv = aInverse ? a : b;
    const auto& normal = aInverse ? b : a;
    // normal.outerBounds() is in device space, so map the inverse path to device space too.
    auto restoredInvPath = inv.getDevicePath();
    restoredInvPath.toggleInverseFillType();
    if (restoredInvPath.contains(normal.outerBounds())) {
      return ClipGeometry::Empty;
    }
  }
  // Two normal elements are disjoint when their bounds do not intersect. Two inverse-fill paths
  // always keep overlapping regions, so their intersection is never empty.
  if (!Rect::Intersects(a.outerBounds(), b.outerBounds())) {
    return ClipGeometry::Empty;
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
      // Let toAdd absorb existing rather than vice versa, so toAdd can accumulate clip data
      // and potentially optimize with more elements in the stack.
      if (toAdd.tryCombine(existing)) {
        existing.markInvalid(static_cast<int>(record.startIndex));
        if (toAdd.shape().isEmpty()) {
          toAdd.markInvalid(static_cast<int>(record.startIndex));
        }
      }
      break;
  }
}

ClipElement::ClipElement() : _shape(Rect::MakeEmpty()) {
}

ClipElement::ClipElement(const GeometryShape& shape, const Matrix& matrix, bool antiAlias)
    : _shape(shape), _matrix(matrix), _antiAlias(antiAlias) {
  simplify();
}

ClipState ClipElement::clipState() const {
  switch (_shape.type()) {
    case ShapeType::Empty:
      return ClipState::Empty;
    case ShapeType::Rect:
      return _matrix.isIdentity() ? ClipState::Rect : ClipState::Complex;
    case ShapeType::RRect:
    case ShapeType::Path:
      return ClipState::Complex;
  }
}

Path ClipElement::getDevicePath() const {
  auto path = _shape.asPath();
  if (!_matrix.isIdentity()) {
    path.transform(_matrix);
  }
  return path;
}

bool ClipElement::tryCombine(const ClipElement& other) {
  // Combining an empty element with any other always succeeds, yielding an empty element.
  if (_shape.isEmpty()) {
    return true;
  }
  if (other._shape.isEmpty()) {
    _shape.setEmpty();
    return true;
  }
  // A Path element or a matrix mismatch prevents combining the two elements cheaply.
  if (_shape.type() == ShapeType::Path || other._shape.type() == ShapeType::Path ||
      _matrix != other._matrix) {
    return false;
  }

  // Case 1: Rect + Rect.
  if (_shape.type() == ShapeType::Rect && other._shape.type() == ShapeType::Rect) {
    if (_antiAlias != other._antiAlias) {
      if (!_matrix.isIdentity()) {
        // Non-identity matrix means the rect may not be axis-aligned in device space, so the two
        // cannot be merged.
        return false;
      }

      if (IsClipPixelAligned(_shape.rect())) {
        // Case 1a: this is pixel-aligned so AA has no visual effect, adopt other's AA flag.
        _antiAlias = other._antiAlias;
      } else if (!IsClipPixelAligned(other._shape.rect())) {
        // Case 1b: neither side is pixel-aligned and AA flags differ, cannot merge.
        return false;
      }
      // Case 1c: other is pixel-aligned, its AA flag is irrelevant, keep this->_antiAlias.
    }

    auto rect = _shape.rect();
    if (!rect.intersect(other._shape.rect())) {
      _shape.setEmpty();
      return true;
    }
    _shape.setRect(rect);
  } else {
    // Case 2: Rect + RRect / RRect + RRect.
    DEBUG_ASSERT(_shape.type() == ShapeType::Rect || _shape.type() == ShapeType::RRect);
    DEBUG_ASSERT(other._shape.type() == ShapeType::Rect || other._shape.type() == ShapeType::RRect);
    // RRect has curved arcs at corners where AA always affects visual output, so unlike Rect there
    // is no pixel-aligned exemption. AA flags must match exactly.
    if (_antiAlias != other._antiAlias) {
      return false;
    }

    const auto thisRRect =
        _shape.type() == ShapeType::Rect ? RRect::MakeRect(_shape.rect()) : _shape.rRect();
    const auto otherRRect = other._shape.type() == ShapeType::Rect
                                ? RRect::MakeRect(other._shape.rect())
                                : other._shape.rRect();
    const auto mergedRRect = RRectUtils::ConservativeIntersect(thisRRect, otherRRect);
    if (!mergedRRect.has_value()) {
      // The intersection cannot be represented as a single RRect (e.g., incompatible corner radii
      // or radii overflow), so we cannot merge the two elements.
      return false;
    }

    if (mergedRRect->rect().isEmpty()) {
      _shape.setEmpty();
      return true;
    }

    if (mergedRRect->type() == RRect::Type::Rect) {
      _shape.setRect(mergedRRect->rect());
    } else {
      _shape.setRRect(*mergedRRect);
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

bool ClipElement::contains(const ClipElement& other) const {
  // The empty region is contained by anything, but contains nothing except another empty region.
  if (other._shape.type() == ShapeType::Empty) {
    return true;
  }
  if (_shape.type() == ShapeType::Empty) {
    return false;
  }

  // If this element's guaranteed-fill region already covers the other's largest possible fill
  // region, this element contains the other.
  if (_innerBounds.contains(other._outerBounds)) {
    return true;
  }

  // Inverse-fill paths keep the region OUTSIDE their bounds, so the normal bounds-containment logic
  // does not apply and they are handled separately.
  if (_shape.type() == ShapeType::Path && _shape.path().isInverseFillType()) {
    if (other._shape.type() == ShapeType::Path && other._shape.path().isInverseFillType()) {
      // When this path's removed region lies inside the other's, this path's keep region contains
      // the other's.
      return other.getDevicePath().contains(getDevicePath().getBounds());
    }
    // This inverse-fill path contains a normal element only when its removed region does not
    // intersect the normal element's keep region.
    return !Rect::Intersects(getDevicePath().getBounds(), other._outerBounds);
  }
  if (other._shape.type() == ShapeType::Path && other._shape.path().isInverseFillType()) {
    // A normal element cannot contain the near-full-plane keep-region of an inverse-fill path.
    return false;
  }

  // When matrix and AA flag match, the two shapes live in the same space and round their edges the
  // same way, so they can be compared directly. This is cheaper than the fallback below and, for
  // rounded or non-rect shapes, more accurate, since the fallback compares against the other's
  // bounding box.
  if (_antiAlias == other._antiAlias && _matrix == other._matrix) {
    if (_shape.type() == ShapeType::RRect && other._shape.type() == ShapeType::RRect) {
      const auto intersected =
          RRectUtils::ConservativeIntersect(_shape.rRect(), other._shape.rRect());
      if (intersected.has_value()) {
        // Strict equality, ignoring floating-point tolerance.
        return *intersected == other._shape.rRect();
      }
    }
    if (_shape.type() == ShapeType::Path && other._shape.type() == ShapeType::Path) {
      return _shape.path().isSame(other._shape.path());
    }
  }

  return ShapeContainsRect(_shape, _matrix, other._shape.bounds(), other._matrix,
                           _antiAlias != other._antiAlias);
}

void ClipElement::transform(const Matrix& matrix) {
  _matrix.postConcat(matrix);
  simplify();
}

void ClipElement::simplify() {
  // First reduce the shape to its simplest type (e.g. a rect-shaped path becomes a Rect), so the
  // matrix folding below can apply to the degenerated rect/rrect.
  _shape.simplify();
  if (_shape.isEmpty()) {
    return;
  }

  // For a Rect or RRect under an axis-aligned matrix, fold the matrix into the geometry and reset
  // it to identity, simplifying later computations. Other cases keep the matrix on the element.
  if (_shape.type() == ShapeType::Rect && _matrix.rectStaysRect()) {
    _shape.setRect(_matrix.mapRect(_shape.rect()));
    _matrix.setIdentity();
  } else if (_shape.type() == ShapeType::RRect && _matrix.rectStaysRect()) {
    auto transformed = RRectUtils::TryAxisAlignedTransform(_shape.rRect(), _matrix);
    DEBUG_ASSERT(transformed.has_value());
    if (transformed.has_value()) {
      _matrix.setIdentity();
      if (transformed->type() == RRect::Type::Rect) {
        _shape.setRect(transformed->rect());
      } else {
        _shape.setRRect(*transformed);
      }
    }
  }

  updateOuterInnerBounds();

  // This can happen for a sub-pixel non-AA rect that covers no pixel center. Covering no pixel
  // makes it equivalent to an empty clip, so mark it empty.
  if (_outerBounds.isEmpty()) {
    _shape.setEmpty();
    _innerBounds = Rect::MakeEmpty();
  }
}

void ClipElement::updateOuterInnerBounds() {
  if (_shape.type() == ShapeType::Empty) {
    _outerBounds = Rect::MakeEmpty();
    _innerBounds = Rect::MakeEmpty();
    return;
  }
  // An inverse-fill path keeps the region OUTSIDE its geometry, so its outer bound is the whole
  // plane regardless of the matrix.
  if (_shape.type() == ShapeType::Path && _shape.path().isInverseFillType()) {
    _outerBounds = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    _innerBounds = Rect::MakeEmpty();
    return;
  }

  Rect outer = _matrix.mapRect(_shape.bounds());
  if (_shape.type() == ShapeType::Rect && _matrix.isIdentity() && !_antiAlias) {
    // A non-AA axis-aligned rect maps exactly onto the integer pixel grid, so rounding it gives a
    // single rect that is both the outer and inner bound. Setting them equal lets the clip run as a
    // pure hardware scissor, skipping the half-pixel expansion GetPixelBounds applies for
    // conservative bounds (which would otherwise split inner and outer by up to a pixel).
    _outerBounds = outer;
    _outerBounds.round();
    _innerBounds = _outerBounds;
  } else {
    _outerBounds = GetPixelBounds(outer, _antiAlias, true);
    if (_shape.type() == ShapeType::Rect && _matrix.isIdentity()) {
      _innerBounds = GetPixelBounds(outer, _antiAlias, false);
    } else if (_shape.type() == ShapeType::RRect && _matrix.isIdentity()) {
      // Identity-matrix RRect: compute the largest inscribed rectangle that avoids the corners.
      _innerBounds = GetPixelBounds(RRectUtils::InnerBounds(_shape.rRect()), _antiAlias, false);
    } else {
      // For non-identity matrices the mapped bounding box is not a reliable inner bound (the actual
      // shape may not fill the entire box), and for Path types the inner region cannot be computed
      // cheaply. Fall back to empty.
      _innerBounds = Rect::MakeEmpty();
    }
  }
}

ClipRecord::ClipRecord() : uniqueID(UniqueID::Next()) {
}

ClipData::ClipData() {
  records.emplace();
}

ClipStack::ClipStack() : _data(std::make_shared<ClipData>()) {
}

void ClipStack::clipShape(GeometryShape&& shape, const Matrix& matrix, bool antiAlias) {
  bool didSave = willModify();
  ClipElement toAdd(std::move(shape), matrix, antiAlias);
  if (addElement(std::move(toAdd)) || !didSave) {
    return;
  }
  // We made a new save record, but ended up not adding an element to the stack.
  // So instead of keeping an empty save record around, pop it off and restore the counter.
  _data->records.pop();
  current().pushSave();
}

void ClipStack::clipPath(const Path& path, const Matrix& matrix, bool antiAlias) {
  clipShape(GeometryShape(path), matrix, antiAlias);
}

void ClipStack::clipRect(const Rect& rect, const Matrix& matrix, bool antiAlias) {
  clipShape(GeometryShape(rect), matrix, antiAlias);
}

void ClipStack::clipRRect(const RRect& rRect, const Matrix& matrix, bool antiAlias) {
  clipShape(GeometryShape(rRect), matrix, antiAlias);
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
  if (toAdd.shape().isEmpty()) {
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
  const ClipElement curElement(GeometryShape(curBoundsPath), Matrix::I(), false);
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
  cur.state = toAdd.clipState();
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
  cur.state = (oldestValidIdx == _data->elements.size()) ? toAdd.clipState() : ClipState::Complex;
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
      result.addPath(element.getDevicePath(), PathOp::Intersect);
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
