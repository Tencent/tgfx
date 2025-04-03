/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "PathSegmentMeasure.h"
#include <include/core/SkPath.h>
#include <include/core/SkPoint.h>
#include <src/core/SkGeometry.h>
#include "PathRef.h"
#include "tgfx/core/Path.h"

namespace tgfx {
using namespace pk;

enum SegmentType { kLine, kQuad, kConic, kCubic };

struct Segment {
  SegmentType type;
  SkPoint pts[4] = {};
  float length;
  float weight;
};

struct Contour {
  std::vector<Segment> segments;
  bool isClosed = false;
  float length = 0;
};

class PkPathPointMeasure : public PathSegmentMeasure {
 public:
  explicit PkPathPointMeasure(const Path& path) {
    auto ref = PathRef::ReadAccess(path);
    iter.setPath(ref, false);
    build();
    contourIt = contours.begin();
    segmentIt = contourIt->segments.begin();
  }

  bool isClosed() override;

  void resetSegement() override;

  void resetContour() override;

  bool nextSegment() override;

  bool nextContour() override;

  float getSegmentLength() override;

  bool getSegment(float startD, float stopD, bool forceMoveTo, Path* path) override;

 private:
  void build();

  std::vector<Contour> contours;
  SkPath::Iter iter;
  std::vector<Contour>::iterator contourIt;
  std::vector<Segment>::iterator segmentIt;
};

static bool QuadTooCurvy(const SkPoint pts[3]) {
  // diff = (a/4 + b/2 + c/4) - (a/2 + c/2)
  // diff = -a/4 + b/2 - c/4
  SkScalar dx = PkScalarHalf(pts[1].fX) - PkScalarHalf(PkScalarHalf(pts[0].fX + pts[2].fX));
  SkScalar dy = PkScalarHalf(pts[1].fY) - PkScalarHalf(PkScalarHalf(pts[0].fY + pts[2].fY));

  SkScalar dist = std::max(PkScalarAbs(dx), PkScalarAbs(dy));
  return dist > 0.5f;
}

static float ComputeQuadLength(const SkPoint pts[3]) {
  if (QuadTooCurvy(pts)) {
    SkPoint tmp[5];
    SkChopQuadAt(pts, tmp, 0.5f);
    return ComputeQuadLength(tmp) + ComputeQuadLength(&tmp[2]);
  }
  return SkPoint::Distance(pts[0], pts[2]);
}

static bool ConicTooCurvy(const SkPoint& firstPt, const SkPoint& midTPt, const SkPoint& lastPt) {
  SkPoint midEnds = firstPt + lastPt;
  midEnds *= 0.5f;
  SkVector dxy = midTPt - midEnds;
  SkScalar dist = std::max(PkScalarAbs(dxy.fX), PkScalarAbs(dxy.fY));
  return dist > 0.5f;
}

static float ComputeConicLength(const SkPoint pts[3], float weight) {
  SkConic conic(pts, weight);
  if (ConicTooCurvy(pts[0], conic.evalAt(0.5f), pts[2])) {
    SkConic tmp[2];
    if (!conic.chopAt(0.5f, tmp)) {
      return 0;
    }
    return ComputeConicLength(tmp[0].fPts, tmp[0].fW) + ComputeConicLength(tmp[1].fPts, tmp[1].fW);
  }
  return SkPoint::Distance(pts[0], pts[2]);
}

static bool CheapDistExceedsLimit(const SkPoint& pt, SkScalar x, SkScalar y) {
  SkScalar dist = std::max(PkScalarAbs(x - pt.fX), PkScalarAbs(y - pt.fY));
  // just made up the 1/2
  return dist > 0.5f;
}

static bool CubicTooCurvy(const SkPoint pts[4]) {
  return CheapDistExceedsLimit(pts[1], SkScalarInterp(pts[0].fX, pts[3].fX, PK_Scalar1 / 3),
                               SkScalarInterp(pts[0].fY, pts[3].fY, PK_Scalar1 / 3)) ||
         CheapDistExceedsLimit(pts[2], SkScalarInterp(pts[0].fX, pts[3].fX, PK_Scalar1 * 2 / 3),
                               SkScalarInterp(pts[0].fY, pts[3].fY, PK_Scalar1 * 2 / 3));
}

static float ComputeCubicLength(const SkPoint pts[4]) {
  if (CubicTooCurvy(pts)) {
    SkPoint tmp[7];
    SkChopCubicAtHalf(pts, tmp);
    return ComputeCubicLength(tmp) + ComputeCubicLength(&tmp[3]);
  }
  return SkPoint::Distance(pts[0], pts[3]);
}

std::shared_ptr<PathSegmentMeasure> PathSegmentMeasure::MakeFrom(const Path& path) {
  return std::make_shared<PkPathPointMeasure>(path);
}

void PkPathPointMeasure::build() {
  SkPoint pts[4];
  SkPath::Verb verb;
  bool hasMoveTo = false;
  while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
    Contour contour;
    auto isClosed = false;
    auto lastPt = SkPoint::Make(0, 0);
    auto fisrtPt = SkPoint::Make(0, 0);
    do {
      if (hasMoveTo && verb == SkPath::kMove_Verb) {
        // another contour
        break;
      }
      Segment segment = {};
      segment.length = 0;
      if (contour.length == 0) {
        fisrtPt = pts[0];
      }
      switch (verb) {
        case SkPath::kLine_Verb:
          segment.type = kLine;
          memcpy(segment.pts, pts, 2 * sizeof(SkPoint));
          segment.length = SkPoint::Distance(pts[0], pts[1]);
          lastPt = pts[1];
          break;

        case SkPath::kQuad_Verb:
          segment.type = kQuad;
          memcpy(segment.pts, pts, 3 * sizeof(SkPoint));
          segment.length = ComputeQuadLength(pts);
          lastPt = pts[2];
          break;

        case SkPath::kConic_Verb:
          segment.type = kConic;
          memcpy(segment.pts, pts, 3 * sizeof(SkPoint));
          segment.weight = iter.conicWeight();
          segment.length = ComputeConicLength(pts, segment.weight);
          lastPt = pts[2];
          break;

        case SkPath::kCubic_Verb:
          segment.type = kCubic;
          memcpy(segment.pts, pts, 4 * sizeof(SkPoint));
          segment.length = ComputeCubicLength(pts);
          lastPt = pts[3];
          break;

        case SkPath::kMove_Verb:
          hasMoveTo = true;
          break;

        case SkPath::kClose_Verb:
          isClosed = true;
          break;

        default:
          continue;
      }

      if (segment.length > 0) {
        contour.length += segment.length;
        contour.segments.push_back(segment);
      }
    } while ((verb = iter.next(pts)) != SkPath::kDone_Verb);
    if (isClosed) {
      contour.isClosed = true;
      auto distance = SkPoint::Distance(lastPt, fisrtPt);
      if (distance > 0) {
        contour.segments.push_back(Segment{kLine, {lastPt, fisrtPt}, distance, 0});
        contour.length += distance;
      }
    }
    if (contour.length > 0) {
      contours.push_back(std::move(contour));
    }
  }
}

bool PkPathPointMeasure::isClosed() {
  return contourIt->isClosed;
}

void PkPathPointMeasure::resetSegement() {
  segmentIt = contourIt->segments.begin();
}

void PkPathPointMeasure::resetContour() {
  contourIt = contours.begin();
  segmentIt = contourIt->segments.begin();
}

bool PkPathPointMeasure::nextSegment() {
  while (segmentIt != contourIt->segments.end()) {
    if ((++segmentIt)->length > 0) {
      return true;
    }
  }
  return false;
}

bool PkPathPointMeasure::nextContour() {
  while (contourIt != contours.end()) {
    if ((++contourIt)->length > 0) {
      segmentIt = contourIt->segments.begin();
      return true;
    }
  }
  return false;
}

float PkPathPointMeasure::getSegmentLength() {
  if (contourIt != contours.end() && segmentIt != contourIt->segments.end()) {
    return segmentIt->length;
  }
  return 0;
}

static void GetLineSegment(const SkPoint pts[2], float startT, float stopT, SkPath* dst,
                           bool forceMoveTo) {
  if (forceMoveTo) {
    SkPoint start = {SkScalarInterp(pts[0].fX, pts[1].fX, startT),
                     SkScalarInterp(pts[0].fY, pts[1].fY, startT)};
    dst->moveTo(start);
  }
  SkPoint end = {SkScalarInterp(pts[0].fX, pts[1].fX, stopT),
                 SkScalarInterp(pts[0].fY, pts[1].fY, stopT)};
  dst->lineTo(end);
}

static void GetQuadSegment(const SkPoint pts[3], float startT, float stopT, SkPath* dst,
                           bool forceMoveTo) {
  if (startT == 0) {
    if (forceMoveTo) {
      dst->moveTo(pts[0]);
    }
    if (stopT == PK_Scalar1) {
      dst->quadTo(pts[1], pts[2]);
    } else {
      SkPoint segPts[5];
      SkChopQuadAt(pts, segPts, stopT);
      dst->quadTo(segPts[1], segPts[2]);
    }
  } else {
    SkPoint tmp0[5];
    SkChopQuadAt(pts, tmp0, startT);
    if (forceMoveTo) {
      dst->moveTo(tmp0[2]);
    }
    if (stopT == PK_Scalar1) {
      dst->quadTo(tmp0[3], tmp0[4]);
    } else {
      SkPoint tmp1[5];
      float adjustedT = (stopT - startT) / (1 - startT);
      SkChopQuadAt(&tmp0[2], tmp1, adjustedT);
      dst->quadTo(tmp1[1], tmp1[2]);
    }
  }
}

static void GetConicSegment(const SkPoint pts[3], SkScalar weight, float startT, float stopT,
                            SkPath* dst, bool forceMoveTo) {
  SkConic conic(pts[0], pts[1], pts[2], weight);
  SkConic tmp;
  conic.chopAt(startT, stopT, &tmp);
  if (forceMoveTo) {
    dst->moveTo(tmp.fPts[0]);
  }
  dst->conicTo(tmp.fPts[1], tmp.fPts[2], tmp.fW);
}

static void GetCubicSegment(const SkPoint pts[4], float startT, float stopT, SkPath* dst,
                            bool forceMoveTo) {
  if (startT == 0) {
    if (forceMoveTo) {
      dst->moveTo(pts[0]);
    }
    if (stopT == PK_Scalar1) {
      dst->cubicTo(pts[1], pts[2], pts[3]);
    } else {
      SkPoint tmp0[7];
      SkChopCubicAt(pts, tmp0, stopT);
      dst->cubicTo(tmp0[1], tmp0[2], tmp0[3]);
    }
  } else {
    SkPoint tmp0[7] = {};
    SkChopCubicAt(pts, tmp0, startT);
    if (forceMoveTo) {
      dst->moveTo(tmp0[3]);
    }
    if (stopT == PK_Scalar1) {
      dst->cubicTo(tmp0[4], tmp0[5], tmp0[6]);
    } else {
      SkPoint tmp1[7] = {};
      float adjustedT = (stopT - startT) / (1 - startT);
      SkChopCubicAt(&tmp0[3], tmp1, adjustedT);
      dst->cubicTo(tmp1[1], tmp1[2], tmp1[3]);
    }
  }
}

bool PkPathPointMeasure::getSegment(float startD, float stopD, bool forceMoveTo, Path* path) {
  if (startD > stopD) {
    return false;
  }
  auto startT = startD / segmentIt->length;
  auto stopT = stopD / segmentIt->length;
  startT = std::max(0.f, std::min(startT, 1.f));
  stopT = std::max(0.f, std::min(stopT, 1.f));
  auto& dst = PathRef::WriteAccess(*path);
  const auto& segment = *segmentIt;
  switch (segment.type) {
    case kLine: {
      GetLineSegment(segment.pts, startT, stopT, &dst, forceMoveTo);
      break;
    }
    case kQuad: {
      GetQuadSegment(segment.pts, startT, stopT, &dst, forceMoveTo);
      break;
    }
    case kConic: {
      GetConicSegment(segment.pts, segment.weight, startT, stopT, &dst, forceMoveTo);
      break;
    }
    case kCubic: {
      GetCubicSegment(segment.pts, startT, stopT, &dst, forceMoveTo);
      break;
    }
  }
  return true;
}
}  // namespace tgfx