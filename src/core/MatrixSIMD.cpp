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

#include <limits>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Vec.h"
// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/MatrixSIMD.cpp"
// Generates code for each enabled target by re-including this source file.
#include "hwy/foreach_target.h"  // IWYU pragma: keep

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
// Must come after foreach_target.h to avoid redefinition errors.
#include "hwy/highway.h"
#pragma clang diagnostic pop

HWY_BEFORE_NAMESPACE();
namespace tgfx {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
void TransPointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count > 0) {
    auto fdst = reinterpret_cast<float*>(&dst[0]);
    const auto fsrc = reinterpret_cast<const float*>(&src[0]);
    const float tx = m.getTranslateX();
    const float ty = m.getTranslateY();
    const HWY_FULL(float) d;
    auto txVec = hn::Set(d, tx);
    auto tyVec = hn::Set(d, ty);
    auto transSimd = hn::InterleaveLower(txVec, tyVec);
    const std::size_t size = static_cast<size_t>(count * 2);
    const std::size_t vecSize = size - size % hn::Lanes(d);
    for (std::size_t i = 0; i < vecSize; i += hn::Lanes(d)) {
      auto srcSimd = hn::LoadU(d, &fsrc[i]);
      auto res = hn::Add(srcSimd, transSimd);
      hn::StoreU(res, d, &fdst[i]);
    }
    for (std::size_t i = vecSize; i < size; i++) {
      fdst[i] = fsrc[i] + (i % 2 == 0 ? tx : ty);
    }
  }
}

void ScalePointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count > 0) {
    auto fdst = reinterpret_cast<float*>(&dst[0]);
    const auto fsrc = reinterpret_cast<const float*>(&src[0]);
    const float tx = m.getTranslateX();
    const float ty = m.getTranslateY();
    const float sx = m.getScaleX();
    const float sy = m.getScaleY();
    const HWY_FULL(float) d;
    auto txVec = hn::Set(d, tx);
    auto tyVec = hn::Set(d, ty);
    auto sxVec = hn::Set(d, sx);
    auto syVec = hn::Set(d, sy);
    auto transSimd = hn::InterleaveLower(txVec, tyVec);
    auto scaleSimd = hn::InterleaveLower(sxVec, syVec);
    const std::size_t size = static_cast<size_t>(count * 2);
    const std::size_t vecSize = size - size % hn::Lanes(d);
    for (std::size_t i = 0; i < vecSize; i += hn::Lanes(d)) {
      auto srcSimd = hn::LoadU(d, &fsrc[i]);
      auto res = hn::MulAdd(srcSimd, scaleSimd, transSimd);
      hn::StoreU(res, d, &fdst[i]);
    }
    for (std::size_t i = vecSize; i < size; i++) {
      fdst[i] = fsrc[i] * (i % 2 == 0 ? sx : sy) + (i % 2 == 0 ? tx : ty);
    }
  }
}

void AffinePointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count > 0) {
    auto fdst = reinterpret_cast<float*>(&dst[0]);
    const auto fsrc = reinterpret_cast<const float*>(&src[0]);
    const float tx = m.getTranslateX();
    const float ty = m.getTranslateY();
    const float sx = m.getScaleX();
    const float sy = m.getScaleY();
    const float kx = m.getSkewX();
    const float ky = m.getSkewY();
    const HWY_FULL(float) d;
    auto txVec = hn::Set(d, tx);
    auto tyVec = hn::Set(d, ty);
    auto sxVec = hn::Set(d, sx);
    auto syVec = hn::Set(d, sy);
    auto kxVec = hn::Set(d, kx);
    auto kyVec = hn::Set(d, ky);
    auto transSimd = hn::InterleaveLower(txVec, tyVec);
    auto scaleSimd = hn::InterleaveLower(sxVec, syVec);
    auto skewSimd = hn::InterleaveLower(kxVec, kyVec);
    const std::size_t size = static_cast<size_t>(count * 2);
    const std::size_t vecSize = size - size % hn::Lanes(d);
    for (std::size_t i = 0; i < vecSize; i += hn::Lanes(d)) {
      auto srcSimd = hn::LoadU(d, &fsrc[i]);
      auto swzSimd = hn::Reverse2(d, srcSimd);
      auto res = hn::MulAdd(srcSimd, scaleSimd, hn::MulAdd(swzSimd, skewSimd, transSimd));
      hn::StoreU(res, d, &fdst[i]);
    }
    float temp = 0.0f;
    for (std::size_t i = vecSize; i < size; i++) {
      float swz = 0.0f;
      const size_t swzindex = (i % 2 == 0 ? i + 1 : i - 1);
      if (fdst == fsrc) {
        swz = swzindex > i ? fsrc[swzindex] : temp;
        temp = fsrc[i];
      } else {
        swz = fsrc[swzindex];
      }
      fdst[i] =
          fsrc[i] * (i % 2 == 0 ? sx : sy) + (i % 2 == 0 ? tx : ty) + swz * (i % 2 == 0 ? kx : ky);
    }
  }
}

void PerspPointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count <= 0) {
    return;
  }

  auto fdst = reinterpret_cast<float*>(&dst[0]);
  const auto fsrc = reinterpret_cast<const float*>(&src[0]);
  const float sx = m.getScaleX();
  const float sy = m.getScaleY();
  const float kx = m.getSkewX();
  const float ky = m.getSkewY();
  const float tx = m.getTranslateX();
  const float ty = m.getTranslateY();
  const float p0 = m.getPerspX();
  const float p1 = m.getPerspY();
  const float p2 = m.get(8);
  const HWY_FULL(float) d;
  auto sxVec = hn::Set(d, sx);
  auto syVec = hn::Set(d, sy);
  auto kxVec = hn::Set(d, kx);
  auto kyVec = hn::Set(d, ky);
  auto txVec = hn::Set(d, tx);
  auto tyVec = hn::Set(d, ty);
  auto p0Vec = hn::Set(d, p0);
  auto p1Vec = hn::Set(d, p1);
  auto p2Vec = hn::Set(d, p2);
  auto scaleSimd = hn::InterleaveLower(sxVec, syVec);
  auto skewSimd = hn::InterleaveLower(kxVec, kyVec);
  auto transSimd = hn::InterleaveLower(txVec, tyVec);
  auto perspSimd = hn::InterleaveLower(p0Vec, p1Vec);
  auto perspSwzSimd = hn::InterleaveLower(p1Vec, p0Vec);
  const std::size_t size = static_cast<size_t>(count * 2);
  const std::size_t vecSize = size - size % hn::Lanes(d);
  for (std::size_t i = 0; i < vecSize; i += hn::Lanes(d)) {
    auto srcSimd = hn::LoadU(d, &fsrc[i]);
    auto swzSimd = hn::Reverse2(d, srcSimd);
    auto xyPrime = hn::MulAdd(srcSimd, scaleSimd, hn::MulAdd(swzSimd, skewSimd, transSimd));
    auto wSimd = hn::MulAdd(srcSimd, perspSimd, hn::MulAdd(swzSimd, perspSwzSimd, p2Vec));
    auto res = hn::Div(xyPrime, wSimd);
    hn::StoreU(res, d, &fdst[i]);
  }
  for (std::size_t i = vecSize; i < size; i += 2) {
    const float x = fsrc[i];
    const float y = fsrc[i + 1];
    float w = x * p0 + y * p1 + p2;
    if (w != 0) {
      w = 1.0f / w;
    }
    fdst[i] = (x * sx + y * kx + tx) * w;
    fdst[i + 1] = (x * ky + y * sy + ty) * w;
  }
}

void ConcatMatrixHWYImpl(const Matrix& first, const Matrix& second, Matrix& dst) {
  const hn::Full128<float> d;
  auto col0 = hn::Dup128VecFromValues(d, second[0], second[3], second[6], 0.0f);
  auto col1 = hn::Dup128VecFromValues(d, second[1], second[4], second[7], 0.0f);
  auto col2 = hn::Dup128VecFromValues(d, second[2], second[5], second[8], 0.0f);
  float result[9];
  for (int i = 0; i < 3; i++) {
    auto row = hn::Dup128VecFromValues(d, first[i * 3], first[i * 3 + 1], first[i * 3 + 2], 0.0f);
    result[i * 3 + 0] = hn::ReduceSum(d, hn::Mul(row, col0));
    result[i * 3 + 1] = hn::ReduceSum(d, hn::Mul(row, col1));
    result[i * 3 + 2] = hn::ReduceSum(d, hn::Mul(row, col2));
  }
  dst.set9(result);
}

Vec3 MapHomogeneousHWYImpl(const Matrix& m, float x, float y, float w) {
  const hn::Full128<float> d;
  auto col0 = hn::Dup128VecFromValues(d, m.getScaleX(), m.getSkewY(), m.getPerspX(), 0.0f);
  auto col1 = hn::Dup128VecFromValues(d, m.getSkewX(), m.getScaleY(), m.getPerspY(), 0.0f);
  auto col2 = hn::Dup128VecFromValues(d, m.getTranslateX(), m.getTranslateY(), m.get(8), 0.0f);
  auto res = hn::MulAdd(col0, hn::Set(d, x),
                        hn::MulAdd(col1, hn::Set(d, y), hn::Mul(col2, hn::Set(d, w))));
  float result[4];
  hn::StoreU(res, d, result);
  return {result[0], result[1], result[2]};
}

// Minimum w value for perspective clipping to avoid division by near-zero values.
constexpr float W0_PLANE_DISTANCE = 1.f / (1 << 14);

// Clips an edge against the w=0 plane. Returns the intersection point's contribution to the
// bounding box as (x, y, -x, -y). If both endpoints are behind the camera, returns infinity.
static auto ClipEdgeToW0Plane(const Vec3& p0, const Vec3& p1) {
  if (p1.z >= W0_PLANE_DISTANCE) {
    const float t = (W0_PLANE_DISTANCE - p0.z) / (p1.z - p0.z);
    const float cx = (t * p1.x + (1.f - t) * p0.x) / W0_PLANE_DISTANCE;
    const float cy = (t * p1.y + (1.f - t) * p0.y) / W0_PLANE_DISTANCE;
    const hn::Full128<float> d;
    return hn::Dup128VecFromValues(d, cx, cy, -cx, -cy);
  }

  const hn::Full128<float> d;
  constexpr float inf = std::numeric_limits<float>::infinity();
  return hn::Set(d, inf);
}

// Projects a corner point with perspective clipping. p0 is the current corner, p1 and p2 are its
// two adjacent corners. Returns (minX, minY, -maxX, -maxY) for bounding box accumulation.
static auto ProjectCornerWithClip(const Vec3& p0, const Vec3& p1, const Vec3& p2) {
  if (p0.z >= W0_PLANE_DISTANCE) {
    const float invW = 1.f / p0.z;
    const float x = p0.x * invW;
    const float y = p0.y * invW;
    const hn::Full128<float> d;
    return hn::Dup128VecFromValues(d, x, y, -x, -y);
  }

  auto c1 = ClipEdgeToW0Plane(p0, p1);
  auto c2 = ClipEdgeToW0Plane(p0, p2);
  return hn::Min(c1, c2);
}

static void MapRectPerspective(const Matrix& m, Rect* dst, const Rect& src) {
  auto tl = MapHomogeneousHWYImpl(m, src.left, src.top, 1.f);
  auto tr = MapHomogeneousHWYImpl(m, src.right, src.top, 1.f);
  auto bl = MapHomogeneousHWYImpl(m, src.left, src.bottom, 1.f);
  auto br = MapHomogeneousHWYImpl(m, src.right, src.bottom, 1.f);

  auto pTL = ProjectCornerWithClip(tl, tr, bl);
  auto pTR = ProjectCornerWithClip(tr, br, tl);
  auto pBR = ProjectCornerWithClip(br, bl, tr);
  auto pBL = ProjectCornerWithClip(bl, tl, br);

  auto minMax = hn::Min(hn::Min(pTL, pTR), hn::Min(pBR, pBL));
  const hn::Full128<float> d;
  float result[4];
  hn::StoreU(minMax, d, result);
  *dst = Rect::MakeLTRB(result[0], result[1], -result[2], -result[3]);
}

void MapRectHWYImpl(const Matrix& m, Rect* dst, const Rect& src) {
  if (m.getType() <= Matrix::TranslateMask) {
    const float tx = m.getTranslateX();
    const float ty = m.getTranslateY();
    const hn::Full128<float> d;
    auto trans = hn::Dup128VecFromValues(d, tx, ty, tx, ty);
    auto ltrb = hn::Add(hn::LoadU(d, &src.left), trans);
    auto rblt = hn::Reverse2(d, hn::Reverse(d, ltrb));
    auto min = hn::Min(ltrb, rblt);
    auto max = hn::Max(ltrb, rblt);
    auto res = hn::ConcatUpperLower(d, max, min);
    hn::StoreU(res, d, &dst->left);
    return;
  }
  if (m.isScaleTranslate()) {
    const float sx = m.getScaleX();
    const float sy = m.getScaleY();
    const float tx = m.getTranslateX();
    const float ty = m.getTranslateY();
    const hn::Full128<float> d;
    auto scale = hn::Dup128VecFromValues(d, sx, sy, sx, sy);
    auto trans = hn::Dup128VecFromValues(d, tx, ty, tx, ty);
    auto ltrb = hn::MulAdd(hn::LoadU(d, &src.left), scale, trans);
    auto rblt = hn::Reverse2(d, hn::Reverse(d, ltrb));
    auto min = hn::Min(ltrb, rblt);
    auto max = hn::Max(ltrb, rblt);
    auto res = hn::ConcatUpperLower(d, max, min);
    hn::StoreU(res, d, &dst->left);
  } else if (m.getType() & Matrix::PerspectiveMask) {
    MapRectPerspective(m, dst, src);
  } else {
    Point quad[4];
    quad[0].set(src.left, src.top);
    quad[1].set(src.right, src.top);
    quad[2].set(src.right, src.bottom);
    quad[3].set(src.left, src.bottom);
    m.mapPoints(quad, quad, 4);
    dst->setBounds(quad, 4);
  }
}

}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {
HWY_EXPORT(TransPointsHWYImpl);
HWY_EXPORT(ScalePointsHWYImpl);
HWY_EXPORT(AffinePointsHWYImpl);
HWY_EXPORT(PerspPointsHWYImpl);
HWY_EXPORT(MapRectHWYImpl);
HWY_EXPORT(ConcatMatrixHWYImpl);
HWY_EXPORT(MapHomogeneousHWYImpl);
void Matrix::TransPoints(const Matrix& m, Point* dst, const Point* src, int count) {
  HWY_DYNAMIC_DISPATCH(TransPointsHWYImpl)(m, dst, src, count);
}

void Matrix::ScalePoints(const Matrix& m, Point dst[], const Point src[], int count) {
  HWY_DYNAMIC_DISPATCH(ScalePointsHWYImpl)(m, dst, src, count);
}

void Matrix::AffinePoints(const Matrix& m, Point dst[], const Point src[], int count) {
  HWY_DYNAMIC_DISPATCH(AffinePointsHWYImpl)(m, dst, src, count);
}

void Matrix::PerspPoints(const Matrix& m, Point dst[], const Point src[], int count) {
  HWY_DYNAMIC_DISPATCH(PerspPointsHWYImpl)(m, dst, src, count);
}

void Matrix::mapRect(Rect* dst, const Rect& src) const {
  HWY_DYNAMIC_DISPATCH(MapRectHWYImpl)(*this, dst, src);
}

void Matrix::ConcatMatrix(const Matrix& first, const Matrix& second, Matrix& dst) {
  HWY_DYNAMIC_DISPATCH(ConcatMatrixHWYImpl)(first, second, dst);
}

Vec3 Matrix::mapHomogeneous(float x, float y, float w) const {
  return HWY_DYNAMIC_DISPATCH(MapHomogeneousHWYImpl)(*this, x, y, w);
}
}  // namespace tgfx
#endif
