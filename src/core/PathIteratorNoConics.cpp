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

#include "PathIteratorNoConics.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include <include/core/SkPath.h>
#pragma clang diagnostic pop
#include "core/PathRef.h"

namespace tgfx {

static_assert(sizeof(pk::SkPath::Iter) <= 64,
              "PathIteratorNoConics storage size is too small for SkPath::Iter");
static_assert(alignof(pk::SkPath::Iter) <= 8,
              "PathIteratorNoConics storage alignment is insufficient for SkPath::Iter");

/////////////////////////////////////////////////////////////////////////////////////////////////
// PathIteratorNoConics implementation
/////////////////////////////////////////////////////////////////////////////////////////////////

PathIteratorNoConics::PathIteratorNoConics(const Path& path) : path(&path) {
}

PathIteratorNoConics::Iterator PathIteratorNoConics::begin() const {
  if (path == nullptr || path->isEmpty()) {
    return Iterator();
  }
  return Iterator(path);
}

/////////////////////////////////////////////////////////////////////////////////////////////////
// PathIteratorNoConics::Iterator implementation
/////////////////////////////////////////////////////////////////////////////////////////////////

PathIteratorNoConics::Iterator::Iterator(const Path* path) : isDone(false) {
  new (storage) pk::SkPath::Iter(PathRef::ReadAccess(*path), false);
  advance();
}

PathIteratorNoConics::Iterator::Iterator() : isDone(true) {
}

PathIteratorNoConics::Iterator::~Iterator() {
  if (!isDone) {
    reinterpret_cast<pk::SkPath::Iter*>(storage)->~Iter();
  }
}

PathIteratorNoConics::Iterator::Iterator(const Iterator& other)
    : current(other.current), hasPendingQuad(other.hasPendingQuad), isDone(other.isDone) {
  if (!isDone) {
    new (storage) pk::SkPath::Iter(*reinterpret_cast<const pk::SkPath::Iter*>(other.storage));
  }
  if (hasPendingQuad) {
    pendingQuad[0] = other.pendingQuad[0];
    pendingQuad[1] = other.pendingQuad[1];
    pendingQuad[2] = other.pendingQuad[2];
  }
}

PathIteratorNoConics::Iterator& PathIteratorNoConics::Iterator::operator=(const Iterator& other) {
  if (this == &other) {
    return *this;
  }
  if (!isDone) {
    reinterpret_cast<pk::SkPath::Iter*>(storage)->~Iter();
  }
  current = other.current;
  hasPendingQuad = other.hasPendingQuad;
  isDone = other.isDone;
  if (!isDone) {
    new (storage) pk::SkPath::Iter(*reinterpret_cast<const pk::SkPath::Iter*>(other.storage));
  }
  if (hasPendingQuad) {
    pendingQuad[0] = other.pendingQuad[0];
    pendingQuad[1] = other.pendingQuad[1];
    pendingQuad[2] = other.pendingQuad[2];
  }
  return *this;
}

PathIteratorNoConics::Iterator& PathIteratorNoConics::Iterator::operator++() {
  advance();
  return *this;
}

void PathIteratorNoConics::Iterator::advance() {
  // First, check if there's a pending quad from conic conversion
  if (hasPendingQuad) {
    hasPendingQuad = false;
    current.verb = PathVerb::Quad;
    current.points[0] = pendingQuad[0];
    current.points[1] = pendingQuad[1];
    current.points[2] = pendingQuad[2];
    return;
  }

  auto* iter = reinterpret_cast<pk::SkPath::Iter*>(storage);
  pk::SkPoint pts[4];
  pk::SkPath::Verb verb = iter->next(pts);

  if (verb == pk::SkPath::kDone_Verb) {
    isDone = true;
    iter->~Iter();
    current = {};
    return;
  }

  if (verb != pk::SkPath::kConic_Verb) {
    current.verb = static_cast<PathVerb>(verb);
    std::memcpy(current.points, pts, sizeof(pts));
    return;
  }

  // Convert conic to quads
  Point points[3];
  std::memcpy(points, pts, sizeof(Point) * 3);
  float weight = iter->conicWeight();
  auto quads = Path::ConvertConicToQuads(points[0], points[1], points[2], weight);
  size_t quadCount = (quads.size() - 1) / 2;

  // Set current to first quad
  current.verb = PathVerb::Quad;
  current.points[0] = points[0];
  current.points[1] = quads[1];
  current.points[2] = quads[2];

  // If there are more quads, save the pending one
  if (quadCount > 1) {
    pendingQuad[0] = quads[2];
    pendingQuad[1] = quads[3];
    pendingQuad[2] = quads[4];
    hasPendingQuad = true;
  }
}

}  // namespace tgfx
