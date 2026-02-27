/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "AdaptiveDashEffect.h"
#include "PathRef.h"
#include "utils/MathExtra.h"

using namespace pk;

namespace tgfx {

#define ADAPTIVE_DASH_SEGMENT_EPSILON 120000
#define ADAPTIVE_DASH_MIN_DASHABLE_LENGTH 0.1f

inline bool IsEven(int x) {
  return !(x & 1);
}

// Structure to hold path segment information
struct Contour {
  std::vector<std::unique_ptr<SkPathMeasure>> segments;
  bool isClosed = false;
  float length = 0.f;
};

// Build contours from path
std::vector<Contour> BuildContours(const Path& path) {
  std::vector<Contour> contours;
  bool hasMoveTo = false;
  auto skPath = PathRef::ReadAccess(path);
  SkPoint pts[4];
  SkPath::Verb verb;
  SkPath::Iter iter(skPath, false);

  while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
    Contour contour;
    bool isClosed = false;
    SkPoint lastPoint = {0, 0};
    SkPoint firstPoint = {0, 0};

    do {
      if (hasMoveTo && verb == SkPath::kMove_Verb) {
        break;  // New contour
      }

      if (contour.segments.empty()) {
        firstPoint = pts[0];
      }

      SkPath segmentPath;
      switch (verb) {
        case SkPath::kLine_Verb:
          segmentPath.moveTo(pts[0]);
          segmentPath.lineTo(pts[1]);
          lastPoint = pts[1];
          break;

        case SkPath::kQuad_Verb:
          segmentPath.moveTo(pts[0]);
          segmentPath.quadTo(pts[1], pts[2]);
          lastPoint = pts[2];
          break;

        case SkPath::kConic_Verb:
          segmentPath.moveTo(pts[0]);
          segmentPath.conicTo(pts[1], pts[2], iter.conicWeight());
          lastPoint = pts[2];
          break;

        case SkPath::kCubic_Verb:
          segmentPath.moveTo(pts[0]);
          segmentPath.cubicTo(pts[1], pts[2], pts[3]);
          lastPoint = pts[3];
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
      auto measure = std::make_unique<SkPathMeasure>(segmentPath, false);
      if (measure->getLength() > 0) {
        contour.length += measure->getLength();
        contour.segments.push_back(std::move(measure));
      }
    } while ((verb = iter.next(pts)) != SkPath::kDone_Verb);

    // Handle closed path
    if (isClosed) {
      contour.isClosed = true;
      if (auto distance = SkPoint::Distance(lastPoint, firstPoint); distance > 0) {
        SkPath closingSegment;
        closingSegment.moveTo(lastPoint);
        closingSegment.lineTo(firstPoint);
        auto measure = std::make_unique<SkPathMeasure>(closingSegment, false);
        if (measure->getLength() > 0) {
          contour.length += measure->getLength();
          contour.segments.push_back(std::move(measure));
        }
      }
    }

    if (!contour.segments.empty()) {
      contours.push_back(std::move(contour));
    }
  }
  return contours;
}

// Normalize phase to [0, intervalLength)
float NormalizePhase(float phase, float intervalLength) {
  if (phase < 0) {
    phase = -phase;
    if (phase > intervalLength) {
      phase = std::fmod(phase, intervalLength);
    }
    phase = intervalLength - phase;
    return (phase == intervalLength) ? 0 : phase;
  }
  return (phase >= intervalLength) ? std::fmod(phase, intervalLength) : phase;
}

AdaptiveDashEffect::AdaptiveDashEffect(const float intervals[], int count, float phase)
    : _intervals(intervals, intervals + count) {
  intervalLength = 0;
  for (float interval : _intervals) {
    intervalLength += interval;
  }
  _phase = NormalizePhase(phase, intervalLength);
}

bool AdaptiveDashEffect::filterPath(Path* path) const {
  if (!path || path->isEmpty()) {
    return false;
  }

  if (intervalLength == 0) {
    path->reset();
    return true;
  }

  const auto contours = BuildContours(*path);
  if (contours.empty()) {
    path->reset();
    return true;
  }

  auto fillType = path->getFillType();
  SkPath resultPath;
  const int patternCount = static_cast<int>(_intervals.size());
  float totalDashCount = 0;

  for (const auto& contour : contours) {
    bool skipFirstSegment = contour.isClosed;
    bool deferFirstSegment = false;
    float deferredLength = 0;
    bool needMoveTo = true;

    for (const auto& segment : contour.segments) {
      const float segmentLength = segment->getLength();
      bool isTinySegment = segmentLength < ADAPTIVE_DASH_MIN_DASHABLE_LENGTH;
      bool isNearlyFullContour = FloatNearlyZero(segmentLength) ||
                                 AreWithinUlps(contour.length - segmentLength, contour.length,
                                               ADAPTIVE_DASH_SEGMENT_EPSILON);

      if (isTinySegment && isNearlyFullContour) {
        // skip the small segments in case some bugs after paths merge.
        if (!needMoveTo) {
          segment->getSegment(0, segmentLength, &resultPath, false);
        } else {
          segment->getSegment(segmentLength, segmentLength, &resultPath, true);
          needMoveTo = false;
        }
        skipFirstSegment = false;
        continue;
      }
      const float patternRatio = std::max(std::roundf(segmentLength / intervalLength), 1.0f);

      // Check dash count limit
      totalDashCount += patternRatio * static_cast<float>(patternCount >> 1);
      if (totalDashCount > MaxDashCount) {
        return false;
      }

      const float scale = segmentLength / (patternRatio * intervalLength);
      float currentPos = -_phase * scale;
      int patternIndex = 0;

      while (currentPos < segmentLength) {
        const float dashLength = _intervals[static_cast<size_t>(patternIndex)] * scale;

        if (IsEven(patternIndex) && currentPos + dashLength > 0) {
          if (currentPos < 0 && skipFirstSegment) {
            deferFirstSegment = true;
            deferredLength = dashLength + currentPos;
          } else {
            segment->getSegment(currentPos, currentPos + dashLength, &resultPath, needMoveTo);
          }
        }

        currentPos += dashLength;
        patternIndex = (patternIndex + 1) % patternCount;
        needMoveTo = true;
      }

      needMoveTo = IsEven(patternIndex);
      skipFirstSegment = false;
    }

    if (deferFirstSegment) {
      contour.segments.front()->getSegment(0, deferredLength, &resultPath, false);
    }
  }

  PathRef::WriteAccess(*path) = resultPath;
  path->setFillType(fillType);
  return true;
}

}  // namespace tgfx
