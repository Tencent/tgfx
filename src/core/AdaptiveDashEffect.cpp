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

inline bool IsEven(int x) {
  return !(x & 1);
}

// Get PointParam for a vertex index
inline PathStroker::PointParam GetParamForVertex(
    int vertexIndex, const std::vector<PathStroker::PointParam>* inputParams,
    const PathStroker::PointParam& defaultParam) {
  if (!inputParams || inputParams->empty() || vertexIndex < 0) {
    return defaultParam;
  }
  // Cycle through params if fewer than vertices
  size_t paramIndex = static_cast<size_t>(vertexIndex) % inputParams->size();
  return (*inputParams)[paramIndex];
}

// Structure to hold path segment information
struct SegmentInfo {
  std::unique_ptr<SkPathMeasure> measure = nullptr;
  int startVertexIndex = -1;
  int endVertexIndex = -1;
};

struct Contour {
  std::vector<SegmentInfo> segments;
  bool isClosed = false;
  float length = 0.f;
};

// Build contours from path
std::vector<Contour> BuildContours(const Path& path, bool trackVertices = false) {
  std::vector<Contour> contours;
  bool hasMoveTo = false;
  auto skPath = PathRef::ReadAccess(path);
  SkPoint pts[4];
  SkPath::Verb verb;
  SkPath::Iter iter(skPath, false);
  int vertexIndex = 0;

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
        SegmentInfo segInfo;
        segInfo.measure = std::move(measure);
        contour.segments.push_back(std::move(segInfo));
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
          SegmentInfo segInfo;
          segInfo.measure = std::move(measure);
          contour.segments.push_back(std::move(segInfo));
        }
      }
    }

    // Set segment vertex indices
    if (trackVertices) {
      auto contourBeginVertexIndex = vertexIndex;
      for (size_t i = 0; i < contour.segments.size(); ++i) {
        contour.segments[i].startVertexIndex = vertexIndex;
        if (i == contour.segments.size() - 1 && isClosed) {
          contour.segments[i].endVertexIndex = contourBeginVertexIndex;
          ++vertexIndex;
        } else {
          contour.segments[i].endVertexIndex = ++vertexIndex;
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
  return onFilterPath(path, nullptr, nullptr);
}

bool AdaptiveDashEffect::onFilterPath(Path* path,
                                      const std::vector<PathStroker::PointParam>* inputParams,
                                      PointParamMapping* outputMapping) const {
  if (!path || path->isEmpty()) {
    return false;
  }

  if (intervalLength == 0) {
    path->reset();
    if (outputMapping) {
      outputMapping->vertexParams.clear();
    }
    return true;
  }

  const bool trackVertices = (inputParams != nullptr && outputMapping != nullptr);
  const auto contours = BuildContours(*path, trackVertices);
  if (contours.empty()) {
    path->reset();
    if (outputMapping) {
      outputMapping->vertexParams.clear();
    }
    return true;
  }

  auto fillType = path->getFillType();
  SkPath resultPath;
  std::vector<PathStroker::PointParam> outputParams;
  const int patternCount = static_cast<int>(_intervals.size());
  float totalDashCount = 0;

  for (const auto& contour : contours) {
    bool skipFirstSegment = contour.isClosed;
    bool deferFirstSegment = false;
    float deferredLength = 0;
    int deferredEndVertexIndex = -1;
    bool needMoveTo = true;

    for (const auto& segInfo : contour.segments) {
      const float segmentLength = segInfo.measure->getLength();
      // Apply ULP-based length comparison to ensure dash generation on tiny contours
      if (FloatNearlyZero(segmentLength) ||
          AreWithinUlps(contour.length - segmentLength, contour.length,
                        ADAPTIVE_DASH_SEGMENT_EPSILON)) {
        // Skip small segments to avoid potential issues after path merging.
        if (!needMoveTo) {
          segInfo.measure->getSegment(0, segmentLength, &resultPath, false);
          if (trackVertices) {
            outputParams.push_back(GetParamForVertex(segInfo.endVertexIndex, inputParams,
                                                     outputMapping->defaultParam));
          }
        } else {
          segInfo.measure->getSegment(segmentLength, segmentLength, &resultPath, true);
          needMoveTo = false;
          if (trackVertices) {
            outputParams.push_back(GetParamForVertex(segInfo.endVertexIndex, inputParams,
                                                     outputMapping->defaultParam));
          }
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
          const float dashStart = std::max(currentPos, 0.0f);
          const float dashEnd = std::min(currentPos + dashLength, segmentLength);

          if (currentPos < 0 && skipFirstSegment) {
            deferFirstSegment = true;
            deferredLength = dashLength + currentPos;
            deferredEndVertexIndex = segInfo.endVertexIndex;
          } else {
            segInfo.measure->getSegment(dashStart, dashEnd, &resultPath, needMoveTo);

            if (trackVertices) {
              // Add param for dash start point (moveTo)
              if (needMoveTo) {
                bool isStartOriginal = FloatNearlyZero(dashStart);
                if (isStartOriginal) {
                  outputParams.push_back(GetParamForVertex(segInfo.startVertexIndex, inputParams,
                                                           outputMapping->defaultParam));
                } else {
                  outputParams.push_back(outputMapping->defaultParam);
                }
              }

              // Add param for dash end point
              bool isEndOriginal = FloatNearlyZero(dashEnd - segmentLength);
              if (isEndOriginal) {
                outputParams.push_back(GetParamForVertex(segInfo.endVertexIndex, inputParams,
                                                         outputMapping->defaultParam));
              } else {
                outputParams.push_back(outputMapping->defaultParam);
              }
            }
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
      contour.segments.front().measure->getSegment(0, deferredLength, &resultPath, false);
      if (trackVertices) {
        bool isEndOriginal =
            FloatNearlyZero(deferredLength - contour.segments.front().measure->getLength());
        if (isEndOriginal) {
          outputParams.push_back(
              GetParamForVertex(deferredEndVertexIndex, inputParams, outputMapping->defaultParam));
        } else {
          outputParams.push_back(outputMapping->defaultParam);
        }
      }
    }
  }

  PathRef::WriteAccess(*path) = resultPath;
  path->setFillType(fillType);

  if (outputMapping) {
    outputMapping->vertexParams = std::move(outputParams);
  }
  return true;
}

}  // namespace tgfx
