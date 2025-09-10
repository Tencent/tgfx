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

#pragma once

#include "core/utils/LazyBounds.h"
#include "gpu/resources/ResourceKey.h"
#include "pathkit.h"

namespace tgfx {
class Path;
struct Rect;

class PathRef {
 public:
  static const pk::SkPath& ReadAccess(const Path& path);

  static pk::SkPath& WriteAccess(Path& path);

  static UniqueKey GetUniqueKey(const Path& path);

  PathRef() = default;

  explicit PathRef(const pk::SkPath& path) : path(path) {
  }

  Rect getBounds();

 private:
  LazyUniqueKey uniqueKey = {};
  LazyBounds bounds = {};
  pk::SkPath path = {};

  friend bool operator==(const Path& a, const Path& b);
  friend bool operator!=(const Path& a, const Path& b);
  friend class Path;
};
}  // namespace tgfx
