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
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include "core/utils/AtomicCache.h"
#include "tgfx/core/Path.h"
#include "tgfx/platform/Print.h"

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

bool PathRef::DiagnosticsEnabled() {
#if DEBUG
  auto value = std::getenv("TGFX_PATH_DIAGNOSTICS");
  return value != nullptr && std::strcmp(value, "1") == 0;
#else
  return false;
#endif
}

void PathRef::TraceDiagnostics(const char* stage, const std::string& details) {
  if (!DiagnosticsEnabled()) {
    return;
  }
  static std::mutex mutex;
  static size_t sequence = 0;
  std::lock_guard<std::mutex> lock(mutex);
  std::ostringstream thread;
  thread << std::this_thread::get_id();
  PrintLog("[PathDiag %zu thread=%s] %s\n%s\n", ++sequence, thread.str().c_str(), stage,
           details.c_str());
}

static const char* DiagnosticFillName(SkPathFillType fillType) {
  switch (fillType) {
    case SkPathFillType::kWinding:
      return "Winding";
    case SkPathFillType::kEvenOdd:
      return "EvenOdd";
    case SkPathFillType::kInverseWinding:
      return "InverseWinding";
    case SkPathFillType::kInverseEvenOdd:
      return "InverseEvenOdd";
  }
  return "Unknown";
}

static void AppendPathDiagnostics(std::ostream& stream, const char* label, const SkPath& original) {
  auto path = original;
  // Reserve on the copy detaches SkPathRef without changing geometry or its RRect metadata.
  // Metadata queries must not populate lazy caches shared by live rendering paths.
  path.incReserve(1);
  const auto& bounds = path.getBounds();
  SkRRect rRect = {};
  auto isRRect = path.isRRect(&rRect);
  stream << std::setprecision(std::numeric_limits<float>::max_digits10);
  stream << label << ": fill=" << DiagnosticFillName(path.getFillType())
         << " finite=" << path.isFinite() << " empty=" << path.isEmpty()
         << " convex=" << path.isConvex() << " verbs=" << path.countVerbs()
         << " points=" << path.countPoints() << " rect=" << path.isRect(nullptr)
         << " oval=" << path.isOval(nullptr) << " rrect=" << isRRect << " bounds=[" << bounds.fLeft
         << ',' << bounds.fTop << ',' << bounds.fRight << ',' << bounds.fBottom << "]\n";
  if (isRRect) {
    const auto& rect = rRect.rect();
    stream << "  rrectBounds=[" << rect.fLeft << ',' << rect.fTop << ',' << rect.fRight << ','
           << rect.fBottom << "] radii=";
    for (int i = 0; i < 4; ++i) {
      auto radius = rRect.radii(static_cast<SkRRect::Corner>(i));
      stream << '(' << radius.fX << ',' << radius.fY << ')';
    }
    stream << '\n';
  }
  SkPath::RawIter iterator(path);
  SkPoint points[4] = {};
  int contour = -1;
  int index = 0;
  for (auto verb = iterator.next(points); verb != SkPath::kDone_Verb;
       verb = iterator.next(points)) {
    const char* name = "Close";
    int count = 0;
    switch (verb) {
      case SkPath::kMove_Verb:
        name = "Move";
        count = 1;
        ++contour;
        break;
      case SkPath::kLine_Verb:
        name = "Line";
        count = 2;
        break;
      case SkPath::kQuad_Verb:
        name = "Quad";
        count = 3;
        break;
      case SkPath::kConic_Verb:
        name = "Conic";
        count = 3;
        break;
      case SkPath::kCubic_Verb:
        name = "Cubic";
        count = 4;
        break;
      default:
        break;
    }
    stream << "  verb=" << index++ << " contour=" << contour << ' ' << name;
    for (int i = 0; i < count; ++i) {
      stream << " (" << points[i].fX << ',' << points[i].fY << ')';
    }
    if (verb == SkPath::kConic_Verb) {
      stream << " weight=" << iterator.conicWeight();
    }
    stream << '\n';
  }
}

void PathRef::TracePath(const char* stage, const Path& path) {
  if (!DiagnosticsEnabled()) {
    return;
  }
  std::ostringstream stream;
  AppendPathDiagnostics(stream, "path", ReadAccess(path));
  TraceDiagnostics(stage, stream.str());
}

void PathRef::TraceUnion(const SkPath& first, const SkPath& second, const SkPath& result,
                         bool concatenates, bool succeeded) {
  if (!DiagnosticsEnabled()) {
    return;
  }
  auto legacyResult = result;
  auto legacySucceeded = succeeded;
  if (concatenates) {
    auto firstCopy = first;
    auto secondCopy = second;
    firstCopy.incReserve(1);
    secondCopy.incReserve(1);
    legacyResult.reset();
    legacySucceeded = Op(firstCopy, secondCopy, SkPathOp::kUnion_SkPathOp, &legacyResult);
  }
  std::ostringstream stream;
  stream << "branch=" << (concatenates ? "bbox-append" : "boolean-op")
         << " actualSuccess=" << succeeded << " legacySuccess=" << legacySucceeded;
  if (succeeded && legacySucceeded) {
    auto sameRule = result;
    sameRule.setFillType(legacyResult.getFillType());
    stream << " samePath=" << (result == legacyResult)
           << " samePathDataIgnoringFill=" << (sameRule == legacyResult);
    auto actualCopy = result;
    auto legacyCopy = legacyResult;
    actualCopy.incReserve(1);
    legacyCopy.incReserve(1);
    SkPath difference;
    auto xorSucceeded = Op(actualCopy, legacyCopy, SkPathOp::kXOR_SkPathOp, &difference);
    stream << " areaXorSuccess=" << xorSucceeded;
    if (xorSucceeded) {
      const auto& bounds = difference.getBounds();
      stream << std::setprecision(std::numeric_limits<float>::max_digits10)
             << " sameArea=" << difference.isEmpty() << " areaXorVerbs=" << difference.countVerbs()
             << " areaXorBounds=[" << bounds.fLeft << ',' << bounds.fTop << ',' << bounds.fRight
             << ',' << bounds.fBottom << ']';
    }
  }
  stream << '\n';
  AppendPathDiagnostics(stream, "first", first);
  AppendPathDiagnostics(stream, "second", second);
  AppendPathDiagnostics(stream, "actual", result);
  if (legacySucceeded) {
    AppendPathDiagnostics(stream, "legacy", legacyResult);
  }
  TraceDiagnostics("Path.addPath.Union", stream.str());
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
