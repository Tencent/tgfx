/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "PathRef.h"
#include "tgfx/core/Path.h"

namespace tgfx {
using namespace pk;

const SkPath& PathRef::ReadAccess(const Path& path) {
  return path.pathRef->path;
}

SkPath& PathRef::WriteAccess(Path& path) {
  return path.writableRef()->path;
}

UniqueKey PathRef::GetUniqueKey(const Path& path) {
  return path.pathRef->uniqueKey.get();
}

PathRef::~PathRef() {
  resetBounds();
}

Rect PathRef::getBounds() {
  auto cacheBounds = bounds.load(std::memory_order_acquire);
  if (cacheBounds == nullptr) {
    // Internally, SkPath lazily computes bounds. Use this function instead of path.getBounds()
    // for thread safety.
    auto count = path.countPoints();
    auto points = new SkPoint[static_cast<size_t>(count)];
    path.getPoints(points, count);
    auto rect = SkRect::MakeEmpty();
    rect.setBounds(points, count);
    delete[] points;
    auto newBounds = new Rect{rect.fLeft, rect.fTop, rect.fRight, rect.fBottom};
    if (bounds.compare_exchange_strong(cacheBounds, newBounds, std::memory_order_acq_rel)) {
      cacheBounds = newBounds;
    } else {
      delete newBounds;
    }
  }
  return *cacheBounds;
}

void PathRef::resetBounds() {
  auto oldBounds = bounds.exchange(nullptr, std::memory_order_acq_rel);
  delete oldBounds;
}
}  // namespace tgfx