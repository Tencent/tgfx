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

#include "core/utils/VecUtils.h"
#include "tgfx/core/Vec.h"
// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/VecSIMD.cpp"
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

Vec4 AddVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  Vec4 result;
  hn::StoreU(hn::Add(va, vb), d, result.ptr());
  return result;
}

Vec4 SubVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  Vec4 result;
  hn::StoreU(hn::Sub(va, vb), d, result.ptr());
  return result;
}

Vec4 MulVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  Vec4 result;
  hn::StoreU(hn::Mul(va, vb), d, result.ptr());
  return result;
}

Vec4 DivVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  Vec4 result;
  hn::StoreU(hn::Div(va, vb), d, result.ptr());
  return result;
}

Vec4 MinVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  Vec4 result;
  hn::StoreU(hn::Min(va, vb), d, result.ptr());
  return result;
}

Vec4 MaxVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  Vec4 result;
  hn::StoreU(hn::Max(va, vb), d, result.ptr());
  return result;
}

Vec4 SqrtVec4HWYImpl(const Vec4& v) {
  const hn::Full128<float> d;
  const auto vec = hn::LoadU(d, v.ptr());
  Vec4 result;
  hn::StoreU(hn::Sqrt(vec), d, result.ptr());
  return result;
}

Vec4 IfThenElseVec4HWYImpl(const Vec4& cond, const Vec4& t, const Vec4& e) {
  const hn::Full128<float> d;
  const auto vc = hn::LoadU(d, cond.ptr());
  const auto vt = hn::LoadU(d, t.ptr());
  const auto ve = hn::LoadU(d, e.ptr());
  // Convert float condition to Highway Mask: any non-zero value is true.
  const auto mask = hn::Ne(vc, hn::Zero(d));
  Vec4 result;
  hn::StoreU(hn::IfThenElse(mask, vt, ve), d, result.ptr());
  return result;
}

bool AnyTrueVec4HWYImpl(const Vec4& v) {
  const hn::Full128<float> d;
  const auto vec = hn::LoadU(d, v.ptr());
  const auto mask = hn::Ne(vec, hn::Zero(d));
  return !hn::AllFalse(d, mask);
}

bool AllTrueVec4HWYImpl(const Vec4& v) {
  const hn::Full128<float> d;
  const auto vec = hn::LoadU(d, v.ptr());
  const auto mask = hn::Ne(vec, hn::Zero(d));
  return hn::AllTrue(d, mask);
}

Vec4 AbsVec4HWYImpl(const Vec4& v) {
  const hn::Full128<float> d;
  const auto vec = hn::LoadU(d, v.ptr());
  Vec4 result;
  hn::StoreU(hn::Abs(vec), d, result.ptr());
  return result;
}

Vec4 OrVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  // Logical OR: result is 1.0f if either operand is non-zero
  const auto maskA = hn::Ne(va, hn::Zero(d));
  const auto maskB = hn::Ne(vb, hn::Zero(d));
  const auto maskOr = hn::Or(maskA, maskB);
  Vec4 result;
  hn::StoreU(hn::IfThenElse(maskOr, hn::Set(d, 1.0f), hn::Zero(d)), d, result.ptr());
  return result;
}

Vec4 AndVec4HWYImpl(const Vec4& a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::LoadU(d, b.ptr());
  // Logical AND: result is 1.0f if both operands are non-zero
  const auto maskA = hn::Ne(va, hn::Zero(d));
  const auto maskB = hn::Ne(vb, hn::Zero(d));
  const auto maskAnd = hn::And(maskA, maskB);
  Vec4 result;
  hn::StoreU(hn::IfThenElse(maskAnd, hn::Set(d, 1.0f), hn::Zero(d)), d, result.ptr());
  return result;
}

Vec4 NegVec4HWYImpl(const Vec4& v) {
  const hn::Full128<float> d;
  const auto vec = hn::LoadU(d, v.ptr());
  Vec4 result;
  hn::StoreU(hn::Neg(vec), d, result.ptr());
  return result;
}

Vec4 MulScalarVec4HWYImpl(const Vec4& a, float b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::Set(d, b);
  Vec4 result;
  hn::StoreU(hn::Mul(va, vb), d, result.ptr());
  return result;
}

Vec4 DivScalarVec4HWYImpl(const Vec4& a, float b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::Set(d, b);
  Vec4 result;
  hn::StoreU(hn::Div(va, vb), d, result.ptr());
  return result;
}

Vec4 ScalarDivVec4HWYImpl(float a, const Vec4& b) {
  const hn::Full128<float> d;
  const auto va = hn::Set(d, a);
  const auto vb = hn::LoadU(d, b.ptr());
  Vec4 result;
  hn::StoreU(hn::Div(va, vb), d, result.ptr());
  return result;
}

Vec4 GeScalarVec4HWYImpl(const Vec4& a, float b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::Set(d, b);
  const auto mask = hn::Ge(va, vb);
  Vec4 result;
  hn::StoreU(hn::IfThenElse(mask, hn::Set(d, 1.0f), hn::Zero(d)), d, result.ptr());
  return result;
}

Vec4 LtScalarVec4HWYImpl(const Vec4& a, float b) {
  const hn::Full128<float> d;
  const auto va = hn::LoadU(d, a.ptr());
  const auto vb = hn::Set(d, b);
  const auto mask = hn::Lt(va, vb);
  Vec4 result;
  hn::StoreU(hn::IfThenElse(mask, hn::Set(d, 1.0f), hn::Zero(d)), d, result.ptr());
  return result;
}

}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {

HWY_EXPORT(AddVec4HWYImpl);
HWY_EXPORT(SubVec4HWYImpl);
HWY_EXPORT(MulVec4HWYImpl);
HWY_EXPORT(DivVec4HWYImpl);
HWY_EXPORT(MinVec4HWYImpl);
HWY_EXPORT(MaxVec4HWYImpl);
HWY_EXPORT(SqrtVec4HWYImpl);
HWY_EXPORT(IfThenElseVec4HWYImpl);
HWY_EXPORT(AnyTrueVec4HWYImpl);
HWY_EXPORT(AllTrueVec4HWYImpl);
HWY_EXPORT(AbsVec4HWYImpl);
HWY_EXPORT(OrVec4HWYImpl);
HWY_EXPORT(AndVec4HWYImpl);
HWY_EXPORT(NegVec4HWYImpl);
HWY_EXPORT(MulScalarVec4HWYImpl);
HWY_EXPORT(DivScalarVec4HWYImpl);
HWY_EXPORT(ScalarDivVec4HWYImpl);
HWY_EXPORT(GeScalarVec4HWYImpl);
HWY_EXPORT(LtScalarVec4HWYImpl);

Vec4 Vec4::operator+(const Vec4& v) const {
  return HWY_DYNAMIC_DISPATCH(AddVec4HWYImpl)(*this, v);
}

Vec4 Vec4::operator-(const Vec4& v) const {
  return HWY_DYNAMIC_DISPATCH(SubVec4HWYImpl)(*this, v);
}

Vec4 Vec4::operator*(const Vec4& v) const {
  return HWY_DYNAMIC_DISPATCH(MulVec4HWYImpl)(*this, v);
}

Vec4 Vec4::operator/(const Vec4& v) const {
  return HWY_DYNAMIC_DISPATCH(DivVec4HWYImpl)(*this, v);
}

Vec4 Min(const Vec4& a, const Vec4& b) {
  return HWY_DYNAMIC_DISPATCH(MinVec4HWYImpl)(a, b);
}

Vec4 Max(const Vec4& a, const Vec4& b) {
  return HWY_DYNAMIC_DISPATCH(MaxVec4HWYImpl)(a, b);
}

Vec4 VecUtils::IfThenElse(const Vec4& cond, const Vec4& t, const Vec4& e) {
  return HWY_DYNAMIC_DISPATCH(IfThenElseVec4HWYImpl)(cond, t, e);
}

bool VecUtils::Any(const Vec4& v) {
  return HWY_DYNAMIC_DISPATCH(AnyTrueVec4HWYImpl)(v);
}

bool VecUtils::All(const Vec4& v) {
  return HWY_DYNAMIC_DISPATCH(AllTrueVec4HWYImpl)(v);
}

Vec4 VecUtils::Abs(const Vec4& v) {
  return HWY_DYNAMIC_DISPATCH(AbsVec4HWYImpl)(v);
}

Vec4 VecUtils::Sqrt(const Vec4& v) {
  return HWY_DYNAMIC_DISPATCH(SqrtVec4HWYImpl)(v);
}

Vec4 VecUtils::Or(const Vec4& a, const Vec4& b) {
  return HWY_DYNAMIC_DISPATCH(OrVec4HWYImpl)(a, b);
}

Vec4 VecUtils::And(const Vec4& a, const Vec4& b) {
  return HWY_DYNAMIC_DISPATCH(AndVec4HWYImpl)(a, b);
}

Vec4 Vec4::operator-() const {
  return HWY_DYNAMIC_DISPATCH(NegVec4HWYImpl)(*this);
}

Vec4 Vec4::operator*(float s) const {
  return HWY_DYNAMIC_DISPATCH(MulScalarVec4HWYImpl)(*this, s);
}

Vec4 Vec4::operator/(float s) const {
  return HWY_DYNAMIC_DISPATCH(DivScalarVec4HWYImpl)(*this, s);
}

Vec4 operator*(float s, const Vec4& v) {
  return HWY_DYNAMIC_DISPATCH(MulScalarVec4HWYImpl)(v, s);
}

Vec4 operator/(float s, const Vec4& v) {
  return HWY_DYNAMIC_DISPATCH(ScalarDivVec4HWYImpl)(s, v);
}

Vec4 VecUtils::GreaterEqual(const Vec4& v, float s) {
  return HWY_DYNAMIC_DISPATCH(GeScalarVec4HWYImpl)(v, s);
}

Vec4 VecUtils::LessThan(const Vec4& v, float s) {
  return HWY_DYNAMIC_DISPATCH(LtScalarVec4HWYImpl)(v, s);
}

}  // namespace tgfx
#endif
