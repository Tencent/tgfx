/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "core/utils/AtomicCache.h"
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
  AtomicCacheReset(bounds);
}

Rect PathRef::getBounds() {
  if (auto cachedBounds = AtomicCacheGet(bounds)) {
    return *cachedBounds;
  }
  // Internally, SkPath lazily computes bounds. Use this function instead of path.getBounds()
  // for thread safety.
  auto count = path.countPoints();
  auto points = new SkPoint[static_cast<size_t>(count)];
  path.getPoints(points, count);
  auto rect = SkRect::MakeEmpty();
  rect.setBounds(points, count);
  delete[] points;
  Rect totalBounds(rect.fLeft, rect.fTop, rect.fRight, rect.fBottom);
  AtomicCacheSet(bounds, &totalBounds);
  return totalBounds;
}
}  // namespace tgfx
