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

#pragma once

/**
  * Vec<N,T> are SIMD vectors of N T's. we're leaning a bit less on platform-specific intrinsics and
  * a bit more on Clang/GCC vector extensions, but still keeping the option open to drop in
  * platform-specific intrinsics.
  */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include "SIMDFeature.h"

/**
  * Users may disable SIMD with TGFX_NO_SIMD, which may be set via compiler flags. Use TGFX_USE_SIMD
  * internally to avoid confusing double negation. Do not use 'defined' in a macro expansion.
  */
#if !defined(TGFX_NO_SIMD)
#define TGFX_USE_SIMD 1
#else
#define TGFX_USE_SIMD 0
#endif

#if TGFX_USE_SIMD
#if TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_AVX
#include <immintrin.h>
#elif TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE41
#include <smmintrin.h>
#elif TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1
#include <emmintrin.h>
#include <xmmintrin.h>
#elif defined(TGFX_ARM_HAS_NEON)
#include <arm_neon.h>
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#elif TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LASX
#include <lasxintrin.h>
#include <lsxintrin.h>
#elif TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
#include <lsxintrin.h>
#endif
#endif

// To avoid ODR violations, all methods must be force-inlined...
#if defined(_MSC_VER)
#define TGFX_ALWAYS_INLINE __forceinline
#else
#define TGFX_ALWAYS_INLINE __attribute__((always_inline))
#endif

/**
 * If T is an 8-byte GCC or Clang vector extension type, it would naturally pass or return in the
 * MMX mm0 register on 32-bit x86 builds.  This has the fun side effect of clobbering any state in
 * the x87 st0 register.  (There is no ABI governing who should preserve mm?/st? registers, so no
 * one does!)
 *
 * We force-inline UnalignedLoad() and UnalignedStore() to avoid that, making them safe to
 * use for all types on all platforms, thus solving the problem once and for all!
 *
 * A separate problem exists with 32-bit x86. The default calling convention returns values in
 * ST0 (the x87 FPU). Unfortunately, doing so can mutate some bit patterns (signaling NaNs
 * become quiet). If you're using these functions to pass data around as floats, but it's actually
 * integers, that can be bad -- raster pipeline does this.
 *
 * With GCC and Clang, the always_inline attribute ensures we don't have a problem. MSVC, though,
 * ignores __forceinline in debug builds, so the return-via-ST0 is always present. Switching to
 * __vectorcall changes the functions to return in xmm0.
 */
#if defined(_MSC_VER) && defined(_M_IX86)
#define TGFX_FP_SAFE_ABI __vectorcall
#else
#define TGFX_FP_SAFE_ABI
#endif

template <typename T, typename P>
static TGFX_ALWAYS_INLINE T TGFX_FP_SAFE_ABI UnalignedLoad(const P* ptr) {
  static_assert(std::is_trivially_copyable_v<P> || std::is_void_v<P>);
  static_assert(std::is_trivially_copyable_v<T>);
  T val;
  // gcc's class-memaccess sometimes triggers when:
  // - `T` is trivially copyable but
  // - `T` is non-trivial (e.g. at least one eligible default constructor is non-trivial).
  // Use `reinterpret_Cast<const void*>` to explicit suppress this warning; a trivially copyable
  // type is safe to memcpy from/to.
  memcpy(&val, static_cast<const void*>(ptr), sizeof(val));
  return val;
}

template <typename T, typename P>
static TGFX_ALWAYS_INLINE void TGFX_FP_SAFE_ABI UnalignedStore(P* ptr, T val) {
  static_assert(std::is_trivially_copyable<T>::value);
  memcpy(ptr, &val, sizeof(val));
}

// Copy the bytes from src into an instance of type Dst and return it.
template <typename Dst, typename Src>
static TGFX_ALWAYS_INLINE Dst TGFX_FP_SAFE_ABI BitCast(const Src& src) {
  static_assert(sizeof(Dst) == sizeof(Src));
  static_assert(std::is_trivially_copyable<Dst>::value);
  static_assert(std::is_trivially_copyable<Src>::value);
  return UnalignedLoad<Dst>(&src);
}

#undef TGFX_FP_SAFE_ABI

// all standalone functions must be static. Please use these helpers:
#define SI static inline
#define SIT             \
  template <typename T> \
  SI
#define SIN        \
  template <int N> \
  SI
#define SINT                   \
  template <int N, typename T> \
  SI
#define SINTU                                                              \
  template <int N, typename T, typename U,                                 \
            typename = std::enable_if_t<std::is_convertible<U, T>::value>> \
  SI

namespace tgfx {

template <int N, typename T>
struct alignas(N * sizeof(T)) Vec;

template <int... Ix, int N, typename T>
SI Vec<sizeof...(Ix), T> Shuffle(const Vec<N, T>&);

// All Vec have the same simple memory layout, the same as `T vec[N]`.
template <int N, typename T>
struct alignas(N * sizeof(T)) Vec {
  static_assert((N & (N - 1)) == 0, "N must be a power of 2.");
  static_assert(sizeof(T) >= alignof(T), "What kind of unusual T is this?");

  // Methods belong here in the class declaration of Vec only if:
  //   - they must be here, like constructors or operator[];
  //   - they'll definitely never want a specialized implementation.
  // Other operations on Vec should be defined outside the type.

  TGFX_ALWAYS_INLINE Vec() = default;
  TGFX_ALWAYS_INLINE Vec(T s) : lo(s), hi(s) {
  }

  // NOTE: Vec{x} produces x000..., whereas Vec(x) produces xxxx.... since this constructor fills
  // unspecified lanes with 0s, whereas the single T constructor fills all lanes with the value.
  TGFX_ALWAYS_INLINE Vec(std::initializer_list<T> xs) {
    T vals[N] = {0};
    assert(xs.size() <= (size_t)N);
    memcpy(vals, xs.begin(), std::min(xs.size(), (size_t)N) * sizeof(T));

    this->lo = Vec<N / 2, T>::Load(vals + 0);
    this->hi = Vec<N / 2, T>::Load(vals + N / 2);
  }

  TGFX_ALWAYS_INLINE T operator[](int i) const {
    return i < N / 2 ? this->lo[i] : this->hi[i - N / 2];
  }
  TGFX_ALWAYS_INLINE T& operator[](int i) {
    return i < N / 2 ? this->lo[i] : this->hi[i - N / 2];
  }

  TGFX_ALWAYS_INLINE static Vec Load(const void* ptr) {
    return UnalignedLoad<Vec>(ptr);
  }
  TGFX_ALWAYS_INLINE void store(void* ptr) const {
    // Note: Calling UnalignedStore produces slightly worse code here, for some reason
    memcpy(ptr, this, sizeof(Vec));
  }

  Vec<N / 2, T> lo, hi;
};

// We have specializations for N == 1 (the base-case), as well as 2 and 4, where we add helpful
// constructors and swizzle accessors.
template <typename T>
struct alignas(4 * sizeof(T)) Vec<4, T> {
  static_assert(sizeof(T) >= alignof(T), "What kind of unusual T is this?");

  TGFX_ALWAYS_INLINE Vec() = default;
  TGFX_ALWAYS_INLINE Vec(T s) : lo(s), hi(s) {
  }
  TGFX_ALWAYS_INLINE Vec(T x, T y, T z, T w) : lo(x, y), hi(z, w) {
  }
  TGFX_ALWAYS_INLINE Vec(Vec<2, T> xy, T z, T w) : lo(xy), hi(z, w) {
  }
  TGFX_ALWAYS_INLINE Vec(T x, T y, Vec<2, T> zw) : lo(x, y), hi(zw) {
  }
  TGFX_ALWAYS_INLINE Vec(Vec<2, T> xy, Vec<2, T> zw) : lo(xy), hi(zw) {
  }

  TGFX_ALWAYS_INLINE Vec(std::initializer_list<T> xs) {
    T vals[4] = {0};
    assert(xs.size() <= (size_t)4);
    memcpy(vals, xs.begin(), std::min(xs.size(), (size_t)4) * sizeof(T));

    this->lo = Vec<2, T>::Load(vals + 0);
    this->hi = Vec<2, T>::Load(vals + 2);
  }

  TGFX_ALWAYS_INLINE T operator[](int i) const {
    return i < 2 ? this->lo[i] : this->hi[i - 2];
  }
  TGFX_ALWAYS_INLINE T& operator[](int i) {
    return i < 2 ? this->lo[i] : this->hi[i - 2];
  }

  TGFX_ALWAYS_INLINE static Vec Load(const void* ptr) {
    return UnalignedLoad<Vec>(ptr);
  }
  TGFX_ALWAYS_INLINE void store(void* ptr) const {
    memcpy(ptr, this, sizeof(Vec));
  }

  TGFX_ALWAYS_INLINE Vec<2, T>& xy() {
    return lo;
  }
  TGFX_ALWAYS_INLINE Vec<2, T>& zw() {
    return hi;
  }
  TGFX_ALWAYS_INLINE T& x() {
    return lo.lo.val;
  }
  TGFX_ALWAYS_INLINE T& y() {
    return lo.hi.val;
  }
  TGFX_ALWAYS_INLINE T& z() {
    return hi.lo.val;
  }
  TGFX_ALWAYS_INLINE T& w() {
    return hi.hi.val;
  }

  TGFX_ALWAYS_INLINE Vec<2, T> xy() const {
    return lo;
  }
  TGFX_ALWAYS_INLINE Vec<2, T> zw() const {
    return hi;
  }
  TGFX_ALWAYS_INLINE T x() const {
    return lo.lo.val;
  }
  TGFX_ALWAYS_INLINE T y() const {
    return lo.hi.val;
  }
  TGFX_ALWAYS_INLINE T z() const {
    return hi.lo.val;
  }
  TGFX_ALWAYS_INLINE T w() const {
    return hi.hi.val;
  }

  // Exchange-based swizzles. These should take 1 cycle on NEON and 3 (pipelined) cycles on SSE.
  TGFX_ALWAYS_INLINE Vec<4, T> yxwz() const {
    return Shuffle<1, 0, 3, 2>(*this);
  }
  TGFX_ALWAYS_INLINE Vec<4, T> zwxy() const {
    return Shuffle<2, 3, 0, 1>(*this);
  }

  Vec<2, T> lo, hi;
};

template <typename T>
struct alignas(2 * sizeof(T)) Vec<2, T> {
  static_assert(sizeof(T) >= alignof(T), "What kind of unusual T is this?");

  TGFX_ALWAYS_INLINE Vec() = default;
  TGFX_ALWAYS_INLINE Vec(T s) : lo(s), hi(s) {
  }
  TGFX_ALWAYS_INLINE Vec(T x, T y) : lo(x), hi(y) {
  }

  TGFX_ALWAYS_INLINE Vec(std::initializer_list<T> xs) {
    T vals[2] = {0};
    assert(xs.size() <= (size_t)2);
    memcpy(vals, xs.begin(), std::min(xs.size(), (size_t)2) * sizeof(T));

    this->lo = Vec<1, T>::Load(vals + 0);
    this->hi = Vec<1, T>::Load(vals + 1);
  }

  TGFX_ALWAYS_INLINE T operator[](int i) const {
    return i < 1 ? this->lo[i] : this->hi[i - 1];
  }
  TGFX_ALWAYS_INLINE T& operator[](int i) {
    return i < 1 ? this->lo[i] : this->hi[i - 1];
  }

  TGFX_ALWAYS_INLINE static Vec Load(const void* ptr) {
    return UnalignedLoad<Vec>(ptr);
  }
  TGFX_ALWAYS_INLINE void store(void* ptr) const {
    memcpy(ptr, this, sizeof(Vec));
  }

  TGFX_ALWAYS_INLINE T& x() {
    return lo.val;
  }
  TGFX_ALWAYS_INLINE T& y() {
    return hi.val;
  }

  TGFX_ALWAYS_INLINE T x() const {
    return lo.val;
  }
  TGFX_ALWAYS_INLINE T y() const {
    return hi.val;
  }

  // This exchange-based swizzle should take 1 cycle on NEON and 3 (pipelined) cycles on SSE.
  TGFX_ALWAYS_INLINE Vec<2, T> yx() const {
    return Shuffle<1, 0>(*this);
  }
  TGFX_ALWAYS_INLINE Vec<4, T> xyxy() const {
    return Vec<4, T>(*this, *this);
  }

  Vec<1, T> lo, hi;
};

template <typename T>
struct Vec<1, T> {
  T val = {};

  TGFX_ALWAYS_INLINE Vec() = default;
  TGFX_ALWAYS_INLINE Vec(T s) : val(s) {
  }

  TGFX_ALWAYS_INLINE Vec(std::initializer_list<T> xs) : val(xs.size() ? *xs.begin() : 0) {
    assert(xs.size() <= (size_t)1);
  }

  TGFX_ALWAYS_INLINE T operator[](int i) const {
    assert(i == 0);
    (void)i;
    return val;
  }
  TGFX_ALWAYS_INLINE T& operator[](int i) {
    assert(i == 0);
    (void)i;
    return val;
  }

  TGFX_ALWAYS_INLINE static Vec Load(const void* ptr) {
    return UnalignedLoad<Vec>(ptr);
  }
  TGFX_ALWAYS_INLINE void store(void* ptr) const {
    memcpy(ptr, this, sizeof(Vec));
  }
};

// Translate from a value type T to its corresponding Mask, the result of a comparison.
template <typename T>
struct Mask {
  using type = T;
};
template <>
struct Mask<float> {
  using type = int32_t;
};
template <>
struct Mask<double> {
  using type = int64_t;
};
template <typename T>
using M = typename Mask<T>::type;

// Join two Vec<N,T> into one Vec<2N,T>.
SINT Vec<2 * N, T> Join(const Vec<N, T>& lo, const Vec<N, T>& hi) {
  Vec<2 * N, T> v;
  v.lo = lo;
  v.hi = hi;
  return v;
}

/**
 * We have three strategies for implementing Vec operations:
 *  1) lean on Clang/GCC vector extensions when available;
 *  2) use map() to apply a scalar function lane-wise;
 *  3) recurse on lo/hi to scalar portable implementations.
 * We can slot in platform-specific implementations as overloads for particular Vec<N,T>,
 * or often integrate them directly into the recursion of style 3), allowing fine control.
 */

#if TGFX_USE_SIMD && (defined(__clang__) || defined(__GNUC__))

// VExt<N,T> types have the same size as Vec<N,T> and support most operations directly.
#if defined(__clang__)
template <int N, typename T>
using VExt = T __attribute__((ext_vector_type(N)));

#elif defined(__GNUC__)
template <int N, typename T>
struct VExtHelper {
  typedef T __attribute__((vector_size(N * sizeof(T)))) type;
};

template <int N, typename T>
using VExt = typename VExtHelper<N, T>::type;

// For some reason some (new!) versions of GCC cannot seem to deduce N in the generic
// ToVec<N,T>() below for N=4 and T=float.  This workaround seems to help...
SI Vec<4, float> ToVec(VExt<4, float> v) {
  return BitCast<Vec<4, float>>(v);
}
#endif

SINT VExt<N, T> ToVext(const Vec<N, T>& v) {
  return BitCast<VExt<N, T>>(v);
}
SINT Vec<N, T> ToVec(const VExt<N, T>& v) {
  return BitCast<Vec<N, T>>(v);
}

SINT Vec<N, T> operator+(const Vec<N, T>& x, const Vec<N, T>& y) {
  return ToVec<N, T>(ToVext(x) + ToVext(y));
}
SINT Vec<N, T> operator-(const Vec<N, T>& x, const Vec<N, T>& y) {
  return ToVec<N, T>(ToVext(x) - ToVext(y));
}
SINT Vec<N, T> operator*(const Vec<N, T>& x, const Vec<N, T>& y) {
  return ToVec<N, T>(ToVext(x) * ToVext(y));
}
SINT Vec<N, T> operator/(const Vec<N, T>& x, const Vec<N, T>& y) {
  return ToVec<N, T>(ToVext(x) / ToVext(y));
}

SINT Vec<N, T> operator^(const Vec<N, T>& x, const Vec<N, T>& y) {
  return ToVec<N, T>(ToVext(x) ^ ToVext(y));
}
SINT Vec<N, T> operator&(const Vec<N, T>& x, const Vec<N, T>& y) {
  return ToVec<N, T>(ToVext(x) & ToVext(y));
}
SINT Vec<N, T> operator|(const Vec<N, T>& x, const Vec<N, T>& y) {
  return ToVec<N, T>(ToVext(x) | ToVext(y));
}

SINT Vec<N, T> operator!(const Vec<N, T>& x) {
  return ToVec<N, T>(!ToVext(x));
}
SINT Vec<N, T> operator-(const Vec<N, T>& x) {
  return ToVec<N, T>(-ToVext(x));
}
SINT Vec<N, T> operator~(const Vec<N, T>& x) {
  return ToVec<N, T>(~ToVext(x));
}

SINT Vec<N, T> operator<<(const Vec<N, T>& x, int k) {
  return ToVec<N, T>(ToVext(x) << k);
}
SINT Vec<N, T> operator>>(const Vec<N, T>& x, int k) {
  return ToVec<N, T>(ToVext(x) >> k);
}

SINT Vec<N, M<T>> operator==(const Vec<N, T>& x, const Vec<N, T>& y) {
  return BitCast<Vec<N, M<T>>>(ToVext(x) == ToVext(y));
}
SINT Vec<N, M<T>> operator!=(const Vec<N, T>& x, const Vec<N, T>& y) {
  return BitCast<Vec<N, M<T>>>(ToVext(x) != ToVext(y));
}
SINT Vec<N, M<T>> operator<=(const Vec<N, T>& x, const Vec<N, T>& y) {
  return BitCast<Vec<N, M<T>>>(ToVext(x) <= ToVext(y));
}
SINT Vec<N, M<T>> operator>=(const Vec<N, T>& x, const Vec<N, T>& y) {
  return BitCast<Vec<N, M<T>>>(ToVext(x) >= ToVext(y));
}
SINT Vec<N, M<T>> operator<(const Vec<N, T>& x, const Vec<N, T>& y) {
  return BitCast<Vec<N, M<T>>>(ToVext(x) < ToVext(y));
}
SINT Vec<N, M<T>> operator>(const Vec<N, T>& x, const Vec<N, T>& y) {
  return BitCast<Vec<N, M<T>>>(ToVext(x) > ToVext(y));
}

#else

// Either TGFX_NO_SIMD is defined, or Clang/GCC vector extensions are not available.
// We'll implement things portably with N==1 scalar implementations and recursion onto them.

// N == 1 scalar implementations.
SIT Vec<1, T> operator+(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val + y.val;
}
SIT Vec<1, T> operator-(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val - y.val;
}
SIT Vec<1, T> operator*(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val * y.val;
}
SIT Vec<1, T> operator/(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val / y.val;
}

SIT Vec<1, T> operator^(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val ^ y.val;
}
SIT Vec<1, T> operator&(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val & y.val;
}
SIT Vec<1, T> operator|(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val | y.val;
}

SIT Vec<1, T> operator!(const Vec<1, T>& x) {
  return !x.val;
}
SIT Vec<1, T> operator-(const Vec<1, T>& x) {
  return -x.val;
}
SIT Vec<1, T> operator~(const Vec<1, T>& x) {
  return ~x.val;
}

SIT Vec<1, T> operator<<(const Vec<1, T>& x, int k) {
  return x.val << k;
}
SIT Vec<1, T> operator>>(const Vec<1, T>& x, int k) {
  return x.val >> k;
}

SIT Vec<1, M<T>> operator==(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val == y.val ? ~0 : 0;
}
SIT Vec<1, M<T>> operator!=(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val != y.val ? ~0 : 0;
}
SIT Vec<1, M<T>> operator<=(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val <= y.val ? ~0 : 0;
}
SIT Vec<1, M<T>> operator>=(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val >= y.val ? ~0 : 0;
}
SIT Vec<1, M<T>> operator<(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val < y.val ? ~0 : 0;
}
SIT Vec<1, M<T>> operator>(const Vec<1, T>& x, const Vec<1, T>& y) {
  return x.val > y.val ? ~0 : 0;
}

// Recurse on lo/hi down to N==1 scalar implementations.
SINT Vec<N, T> operator+(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo + y.lo, x.hi + y.hi);
}
SINT Vec<N, T> operator-(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo - y.lo, x.hi - y.hi);
}
SINT Vec<N, T> operator*(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo * y.lo, x.hi * y.hi);
}
SINT Vec<N, T> operator/(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo / y.lo, x.hi / y.hi);
}

SINT Vec<N, T> operator^(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo ^ y.lo, x.hi ^ y.hi);
}
SINT Vec<N, T> operator&(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo & y.lo, x.hi & y.hi);
}
SINT Vec<N, T> operator|(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo | y.lo, x.hi | y.hi);
}

SINT Vec<N, T> operator!(const Vec<N, T>& x) {
  return Join(!x.lo, !x.hi);
}
SINT Vec<N, T> operator-(const Vec<N, T>& x) {
  return Join(-x.lo, -x.hi);
}
SINT Vec<N, T> operator~(const Vec<N, T>& x) {
  return Join(~x.lo, ~x.hi);
}

SINT Vec<N, T> operator<<(const Vec<N, T>& x, int k) {
  return Join(x.lo << k, x.hi << k);
}
SINT Vec<N, T> operator>>(const Vec<N, T>& x, int k) {
  return Join(x.lo >> k, x.hi >> k);
}

SINT Vec<N, M<T>> operator==(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo == y.lo, x.hi == y.hi);
}
SINT Vec<N, M<T>> operator!=(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo != y.lo, x.hi != y.hi);
}
SINT Vec<N, M<T>> operator<=(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo <= y.lo, x.hi <= y.hi);
}
SINT Vec<N, M<T>> operator>=(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo >= y.lo, x.hi >= y.hi);
}
SINT Vec<N, M<T>> operator<(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo < y.lo, x.hi < y.hi);
}
SINT Vec<N, M<T>> operator>(const Vec<N, T>& x, const Vec<N, T>& y) {
  return Join(x.lo > y.lo, x.hi > y.hi);
}
#endif

// Scalar/vector operations splat the scalar to a vector.
SINTU Vec<N, T> operator+(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) + y;
}
SINTU Vec<N, T> operator-(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) - y;
}
SINTU Vec<N, T> operator*(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) * y;
}
SINTU Vec<N, T> operator/(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) / y;
}
SINTU Vec<N, T> operator^(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) ^ y;
}
SINTU Vec<N, T> operator&(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) & y;
}
SINTU Vec<N, T> operator|(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) | y;
}
SINTU Vec<N, M<T>> operator==(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) == y;
}
SINTU Vec<N, M<T>> operator!=(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) != y;
}
SINTU Vec<N, M<T>> operator<=(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) <= y;
}
SINTU Vec<N, M<T>> operator>=(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) >= y;
}
SINTU Vec<N, M<T>> operator<(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) < y;
}
SINTU Vec<N, M<T>> operator>(U x, const Vec<N, T>& y) {
  return Vec<N, T>(x) > y;
}

SINTU Vec<N, T> operator+(const Vec<N, T>& x, U y) {
  return x + Vec<N, T>(y);
}
SINTU Vec<N, T> operator-(const Vec<N, T>& x, U y) {
  return x - Vec<N, T>(y);
}
SINTU Vec<N, T> operator*(const Vec<N, T>& x, U y) {
  return x * Vec<N, T>(y);
}
SINTU Vec<N, T> operator/(const Vec<N, T>& x, U y) {
  return x / Vec<N, T>(y);
}
SINTU Vec<N, T> operator^(const Vec<N, T>& x, U y) {
  return x ^ Vec<N, T>(y);
}
SINTU Vec<N, T> operator&(const Vec<N, T>& x, U y) {
  return x & Vec<N, T>(y);
}
SINTU Vec<N, T> operator|(const Vec<N, T>& x, U y) {
  return x | Vec<N, T>(y);
}
SINTU Vec<N, M<T>> operator==(const Vec<N, T>& x, U y) {
  return x == Vec<N, T>(y);
}
SINTU Vec<N, M<T>> operator!=(const Vec<N, T>& x, U y) {
  return x != Vec<N, T>(y);
}
SINTU Vec<N, M<T>> operator<=(const Vec<N, T>& x, U y) {
  return x <= Vec<N, T>(y);
}
SINTU Vec<N, M<T>> operator>=(const Vec<N, T>& x, U y) {
  return x >= Vec<N, T>(y);
}
SINTU Vec<N, M<T>> operator<(const Vec<N, T>& x, U y) {
  return x < Vec<N, T>(y);
}
SINTU Vec<N, M<T>> operator>(const Vec<N, T>& x, U y) {
  return x > Vec<N, T>(y);
}

SINT Vec<N, T>& operator+=(Vec<N, T>& x, const Vec<N, T>& y) {
  return (x = x + y);
}
SINT Vec<N, T>& operator-=(Vec<N, T>& x, const Vec<N, T>& y) {
  return (x = x - y);
}
SINT Vec<N, T>& operator*=(Vec<N, T>& x, const Vec<N, T>& y) {
  return (x = x * y);
}
SINT Vec<N, T>& operator/=(Vec<N, T>& x, const Vec<N, T>& y) {
  return (x = x / y);
}
SINT Vec<N, T>& operator^=(Vec<N, T>& x, const Vec<N, T>& y) {
  return (x = x ^ y);
}
SINT Vec<N, T>& operator&=(Vec<N, T>& x, const Vec<N, T>& y) {
  return (x = x & y);
}
SINT Vec<N, T>& operator|=(Vec<N, T>& x, const Vec<N, T>& y) {
  return (x = x | y);
}

SINTU Vec<N, T>& operator+=(Vec<N, T>& x, U y) {
  return (x = x + Vec<N, T>(y));
}
SINTU Vec<N, T>& operator-=(Vec<N, T>& x, U y) {
  return (x = x - Vec<N, T>(y));
}
SINTU Vec<N, T>& operator*=(Vec<N, T>& x, U y) {
  return (x = x * Vec<N, T>(y));
}
SINTU Vec<N, T>& operator/=(Vec<N, T>& x, U y) {
  return (x = x / Vec<N, T>(y));
}
SINTU Vec<N, T>& operator^=(Vec<N, T>& x, U y) {
  return (x = x ^ Vec<N, T>(y));
}
SINTU Vec<N, T>& operator&=(Vec<N, T>& x, U y) {
  return (x = x & Vec<N, T>(y));
}
SINTU Vec<N, T>& operator|=(Vec<N, T>& x, U y) {
  return (x = x | Vec<N, T>(y));
}

SINT Vec<N, T>& operator<<=(Vec<N, T>& x, int bits) {
  return (x = x << bits);
}
SINT Vec<N, T>& operator>>=(Vec<N, T>& x, int bits) {
  return (x = x >> bits);
}

// Some operations we want are not expressible with Clang/GCC vector extensions.

// Clang can reason about NaiveIfThenElse() and optimize through it better than IfThenElse(), so
// it's sometimes useful to call it directly when we think an entire expression should optimize
// away, e.g. min()/max().
SINT Vec<N, T> NaiveIfThenElse(const Vec<N, M<T>>& cond, const Vec<N, T>& t, const Vec<N, T>& e) {
  return BitCast<Vec<N, T>>((cond & BitCast<Vec<N, M<T>>>(t)) | (~cond & BitCast<Vec<N, M<T>>>(e)));
}

SINT Vec<N, T> IfThenElse(const Vec<N, M<T>>& cond, const Vec<N, T>& t, const Vec<N, T>& e) {
  // Specializations inline here so they can generalize what types the apply to.
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_AVX2
  if constexpr (N * sizeof(T) == 32) {
    return BitCast<Vec<N, T>>(
        _mm256_blendv_epi8(BitCast<__m256i>(e), BitCast<__m256i>(t), BitCast<__m256i>(cond)));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE41
  if constexpr (N * sizeof(T) == 16) {
    return BitCast<Vec<N, T>>(
        _mm_blendv_epi8(BitCast<__m128i>(e), BitCast<__m128i>(t), BitCast<__m128i>(cond)));
  }
#endif
#if TGFX_USE_SIMD && defined(TGFX_ARM_HAS_NEON)
  if constexpr (N * sizeof(T) == 16) {
    return BitCast<Vec<N, T>>(
        vbslq_u8(BitCast<uint8x16_t>(cond), BitCast<uint8x16_t>(t), BitCast<uint8x16_t>(e)));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LASX
  if constexpr (N * sizeof(T) == 32) {
    return BitCast<Vec<N, T>>(
        __lasx_xvbitsel_v(BitCast<__m256i>(e), BitCast<__m256i>(t), BitCast<__m256i>(cond)));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
  if constexpr (N * sizeof(T) == 16) {
    return BitCast<Vec<N, T>>(
        __lsx_vbitsel_v(BitCast<__m128i>(e), BitCast<__m128i>(t), BitCast<__m128i>(cond)));
  }
#endif
  // Recurse for large vectors to try to hit the specializations above.
  if constexpr (N * sizeof(T) > 16) {
    return Join(IfThenElse(cond.lo, t.lo, e.lo), IfThenElse(cond.hi, t.hi, e.hi));
  }
  // This default can lead to better code than the recursing onto scalars.
  return NaiveIfThenElse(cond, t, e);
}

SIT bool Any(const Vec<1, T>& x) {
  return x.val != 0;
}
SINT bool Any(const Vec<N, T>& x) {
  // For Any(), the _mm_testz intrinsics are correct and don't require comparing 'x' to 0, so it's
  // lower latency compared to _mm_movemask + _mm_compneq on plain SSE.
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_AVX2
  if constexpr (N * sizeof(T) == 32) {
    return !_mm256_testz_si256(BitCast<__m256i>(x), _mm256_set1_epi32(-1));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE41
  if constexpr (N * sizeof(T) == 16) {
    return !_mm_testz_si128(BitCast<__m128i>(x), _mm_set1_epi32(-1));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1
  if constexpr (N * sizeof(T) == 16) {
    // On SSE, movemaTGFX checks only the MSB in each lane, which is fine if the lanes were set
    // directly from a comparison op (which sets all bits to 1 when true), but TGFX::Vec<>
    // treats any non-zero value as true, so we have to compare 'x' to 0 before calling movemask
    return _mm_movemask_ps(_mm_cmpneq_ps(BitCast<__m128>(x), _mm_set1_ps(0))) != 0b0000;
  }
#endif
#if TGFX_USE_SIMD && defined(__aarch64__)
  // On 64-bit NEON, take the max aCross lanes, which will be non-zero if any lane was true.
  // The specific lane-size doesn't really matter in this case since it's really any set bit
  // that we're looking for.
  if constexpr (N * sizeof(T) == 8) {
    return vmaxv_u8(BitCast<uint8x8_t>(x)) > 0;
  }
  if constexpr (N * sizeof(T) == 16) {
    return vmaxvq_u8(BitCast<uint8x16_t>(x)) > 0;
  }
#endif
#if TGFX_USE_SIMD && defined(__wasm_simd128__)
  if constexpr (N == 4 && sizeof(T) == 4) {
    return wasm_i32x4_any_true(BitCast<VExt<4, int>>(x));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LASX
  if constexpr (N * sizeof(T) == 32) {
    v8i32 retv = (v8i32)__lasx_xvmTGFXltz_w(__lasx_xvslt_wu(__lasx_xvldi(0), BitCast<__m256i>(x)));
    return (retv[0] | retv[4]) != 0b0000;
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
  if constexpr (N * sizeof(T) == 16) {
    v4i32 retv = (v4i32)__lsx_vmTGFXltz_w(__lsx_vslt_wu(__lsx_vldi(0), BitCast<__m128i>(x)));
    return retv[0] != 0b0000;
  }
#endif
  return Any(x.lo) || Any(x.hi);
}

SIT bool All(const Vec<1, T>& x) {
  return x.val != 0;
}
SINT bool All(const Vec<N, T>& x) {
// Unlike Any(), we have to respect the lane layout, or we'll miss cases where a true lane has a
// mix of 0 and 1 bits.
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1
  // Unfortunately, the _mm_testc intrinsics don't let us avoid the comparison to 0 for all()'s
  // correctness, so always just use the plain SSE version.
  if constexpr (N == 4 && sizeof(T) == 4) {
    return _mm_movemask_ps(_mm_cmpneq_ps(BitCast<__m128>(x), _mm_set1_ps(0))) == 0b1111;
  }
#endif
#if TGFX_USE_SIMD && defined(__aarch64__)
  // On 64-bit NEON, take the min aCross the lanes, which will be non-zero if all lanes are != 0.
  if constexpr (sizeof(T) == 1 && N == 8) {
    return vminv_u8(BitCast<uint8x8_t>(x)) > 0;
  }
  if constexpr (sizeof(T) == 1 && N == 16) {
    return vminvq_u8(BitCast<uint8x16_t>(x)) > 0;
  }
  if constexpr (sizeof(T) == 2 && N == 4) {
    return vminv_u16(BitCast<uint16x4_t>(x)) > 0;
  }
  if constexpr (sizeof(T) == 2 && N == 8) {
    return vminvq_u16(BitCast<uint16x8_t>(x)) > 0;
  }
  if constexpr (sizeof(T) == 4 && N == 2) {
    return vminv_u32(BitCast<uint32x2_t>(x)) > 0;
  }
  if constexpr (sizeof(T) == 4 && N == 4) {
    return vminvq_u32(BitCast<uint32x4_t>(x)) > 0;
  }
#endif
#if TGFX_USE_SIMD && defined(__wasm_simd128__)
  if constexpr (N == 4 && sizeof(T) == 4) {
    return wasm_i32x4_all_true(BitCast<VExt<4, int>>(x));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LASX
  if constexpr (N == 8 && sizeof(T) == 4) {
    v8i32 retv = (v8i32)__lasx_xvmTGFXltz_w(__lasx_xvslt_wu(__lasx_xvldi(0), BitCast<__m256i>(x)));
    return (retv[0] & retv[4]) == 0b1111;
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
  if constexpr (N == 4 && sizeof(T) == 4) {
    v4i32 retv = (v4i32)__lsx_vmTGFXltz_w(__lsx_vslt_wu(__lsx_vldi(0), BitCast<__m128i>(x)));
    return retv[0] == 0b1111;
  }
#endif
  return All(x.lo) && All(x.hi);
}

// Cast() Vec<N,S> to Vec<N,D>, as if applying a C-Cast to each lane.
template <typename D, typename S>
SI Vec<1, D> Cast(const Vec<1, S>& src) {
  return (D)src.val;
}

template <typename D, int N, typename S>
SI Vec<N, D> Cast(const Vec<N, S>& src) {
#if TGFX_USE_SIMD && defined(__clang__)
  return ToVec(__builtin_convertvector(ToVext(src), VExt<N, D>));
#else
  return Join(Cast<D>(src.lo), Cast<D>(src.hi));
#endif
}

// Min/Max match logic of std::min/std::max, which is important when NaN is involved.
SIT T Min(const Vec<1, T>& x) {
  return x.val;
}
SIT T Max(const Vec<1, T>& x) {
  return x.val;
}
SINT T Min(const Vec<N, T>& x) {
  return std::min(Min(x.lo), Min(x.hi));
}
SINT T Max(const Vec<N, T>& x) {
  return std::max(Max(x.lo), Max(x.hi));
}

SINT Vec<N, T> Min(const Vec<N, T>& x, const Vec<N, T>& y) {
  return IfThenElse(y < x, y, x);
}
SINT Vec<N, T> Max(const Vec<N, T>& x, const Vec<N, T>& y) {
  return IfThenElse(x < y, y, x);
}

SINTU Vec<N, T> Min(const Vec<N, T>& x, U y) {
  return Min(x, Vec<N, T>(y));
}
SINTU Vec<N, T> Max(const Vec<N, T>& x, U y) {
  return Max(x, Vec<N, T>(y));
}
SINTU Vec<N, T> Min(U x, const Vec<N, T>& y) {
  return Min(Vec<N, T>(x), y);
}
SINTU Vec<N, T> Max(U x, const Vec<N, T>& y) {
  return Max(Vec<N, T>(x), y);
}

// Pin is important when NaN is involved. It always returns values in the range lo..hi, and if x is
// NaN, it returns lo.
SINT Vec<N, T> Pin(const Vec<N, T>& x, const Vec<N, T>& lo, const Vec<N, T>& hi) {
  return Max(lo, Min(x, hi));
}

// Shuffle values from a vector pretty arbitrarily:
//    Vec<4,float> rgba = {R,G,B,A};
//    Shuffle<2,1,0,3>        (rgba) ~> {B,G,R,A}
//    Shuffle<2,1>            (rgba) ~> {B,G}
//    Shuffle<2,1,2,1,2,1,2,1>(rgba) ~> {B,G,B,G,B,G,B,G}
//    Shuffle<3,3,3,3>        (rgba) ~> {A,A,A,A}
// The only real restriction is that the output also be a legal N=power-of-two Vec.
template <int... Ix, int N, typename T>
SI Vec<sizeof...(Ix), T> Shuffle(const Vec<N, T>& x) {
#if TGFX_USE_SIMD && defined(__clang__)
  return ToVec<sizeof...(Ix), T>(__builtin_shufflevector(ToVext(x), ToVext(x), Ix...));
#else
  return {x[Ix]...};
#endif
}

// Call Map(fn, x) for a vector with fn() applied to each lane of x, { fn(x[0]), fn(x[1]), ... },
// or Map(fn, x,y) for a vector of fn(x[i], y[i]), etc.

/**
 * Used to ignore sanitizer warnings.
 */

#if defined(__clang__) || defined(__GNUC__)
#define TGFX_ATTRIBUTE(attr) __attribute__((attr))
#else
#define TGFX_ATTRIBUTE(attr)
#endif

#if !defined(TGFX_NO_SANITIZE)
#if defined(__has_attribute)
#if __has_attribute(no_sanitize)
// This should be for clang and versions of gcc >= 8.0
#define TGFX_NO_SANITIZE(A) TGFX_ATTRIBUTE(no_sanitize(A))
#else
// For compilers that don't support sanitization, just do nothing.
#define TGFX_NO_SANITIZE(A)
#endif
#else  // no __has_attribute, e.g. MSVC
#define TGFX_NO_SANITIZE(A)
#endif
#endif

#if defined(__clang__)
#define TGFX_NO_SANITIZE_CFI TGFX_NO_SANITIZE("cfi")
#else
#define TGFX_NO_SANITIZE_CFI
#endif

template <typename Fn, typename... Args, size_t... I>
SI auto Map(std::index_sequence<I...>, Fn&& fn, const Args&... args)
    -> Vec<sizeof...(I), decltype(fn(args[0]...))> {
  auto lane = [&](size_t i)
      // CFI, specifically -fsanitize=cfi-icall, seems to give a false positive here, with errors
      // like "control flow integrity check for type 'float (float) noexcept' failed during indirect
      // function call... note: sqrtf.cfi_jt defined here".  But we can be quite sure fn is the
      // right type: it's all inferred! So, stifle CFI in this function.
      TGFX_NO_SANITIZE_CFI { return fn(args[static_cast<int>(i)]...); };

  return {lane(I)...};
}

template <typename Fn, int N, typename T, typename... Rest>
auto Map(Fn&& fn, const Vec<N, T>& first, const Rest&... rest) {
  // Derive an {0...N-1} index_sequence from the size of the first arg: N lanes in, N lanes out.
  return Map(std::make_index_sequence<N>{}, fn, first, rest...);
}

SIN Vec<N, float> Ceil(const Vec<N, float>& x) {
  return Map(ceilf, x);
}
SIN Vec<N, float> Floor(const Vec<N, float>& x) {
  return Map(floorf, x);
}
SIN Vec<N, float> Trunc(const Vec<N, float>& x) {
  return Map(truncf, x);
}
SIN Vec<N, float> Round(const Vec<N, float>& x) {
  return Map(roundf, x);
}
SIN Vec<N, float> Sqrt(const Vec<N, float>& x) {
  return Map(sqrtf, x);
}
SIN Vec<N, float> Abs(const Vec<N, float>& x) {
  return Map(fabsf, x);
}
SIN Vec<N, float> Fma(const Vec<N, float>& x, const Vec<N, float>& y, const Vec<N, float>& z) {
  // I don't understand why Clang's codegen is terrible if we write map(fmaf, x,y,z) directly.
  auto fn = [](float x, float y, float z) { return fmaf(x, y, z); };
  return Map(fn, x, y, z);
}

SI Vec<1, int> Lrint(const Vec<1, float>& x) {
  return (int)lrintf(x.val);
}
SIN Vec<N, int> Lrint(const Vec<N, float>& x) {
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_AVX
  if constexpr (N == 8) {
    return BitCast<Vec<N, int>>(_mm256_cvtps_epi32(BitCast<__m256>(x)));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1
  if constexpr (N == 4) {
    return BitCast<Vec<N, int>>(_mm_cvtps_epi32(BitCast<__m128>(x)));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LASX
  if constexpr (N == 8) {
    return BitCast<Vec<N, int>>(__lasx_xvftint_w_s(BitCast<__m256>(x)));
  }
#endif
#if TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
  if constexpr (N == 4) {
    return BitCast<Vec<N, int>>(__lsx_vftint_w_s(BitCast<__m128>(x)));
  }
#endif
  return Join(Lrint(x.lo), Lrint(x.hi));
}

SIN Vec<N, float> Fract(const Vec<N, float>& x) {
  return x - Floor(x);
}

SIN Vec<N, uint16_t> ToHalf(const Vec<N, float>& x) {
  assert(All(x == x));  // No NaNs should reach this function

  // Intrinsics for float->half tend to operate on 4 lanes, and the default implementation has
  // enough instructions that it's better to split and Join on 128 bits groups vs. recursing for
  // each min/max/shift/etc.
  if constexpr (N > 4) {
    return Join(ToHalf(x.lo), ToHalf(x.hi));
  }

#if TGFX_USE_SIMD && defined(__aarch64__)
  if constexpr (N == 4) {
    return BitCast<Vec<N, uint16_t>>(vcvt_f16_f32(BitCast<float32x4_t>(x)));
  }
#endif

#define I(x) BitCast<Vec<N, int32_t>>(x)
#define F(x) BitCast<Vec<N, float>>(x)
  Vec<N, int32_t> sem = I(x), s = sem & 0x8000'0000, em = Min(sem ^ s, 0x4780'0000),
                  magic = I(Max(F(em) * 8192.f, 0.5f)) & (255 << 23),
                  rounded = I((F(em) + F(magic))),
                  exp = ((magic >> 13) - ((127 - 15 + 13 + 1) << 10)), f16 = rounded + exp;
  return Cast<uint16_t>((s >> 16) | f16);
#undef I
#undef F
}

// Converts from half to float, preserving NaN and +/- infinity.
SIN Vec<N, float> FromHalf(const Vec<N, uint16_t>& x) {
  if constexpr (N > 4) {
    return Join(FromHalf(x.lo), FromHalf(x.hi));
  }

#if TGFX_USE_SIMD && defined(__aarch64__)
  if constexpr (N == 4) {
    return BitCast<Vec<N, float>>(vcvt_f32_f16(BitCast<float16x4_t>(x)));
  }
#endif

  Vec<N, int32_t> wide = Cast<int32_t>(x), s = wide & 0x8000, em = wide ^ s,
                  inf_or_nan = (em >= (31 << 10)) & (255 << 23), is_norm = em > 0x3ff,
                  sub = BitCast<Vec<N, int32_t>>((Cast<float>(em) * (1.f / (1 << 24)))),
                  norm = ((em << 13) + ((127 - 15) << 23)),
                  finite = (is_norm & norm) | (~is_norm & sub);
  return BitCast<Vec<N, float>>((s << 16) | finite | inf_or_nan);
}

// Div255(x) = (x + 127) / 255 is a bit-exact rounding divide-by-255, packing down to 8-bit.
SIN Vec<N, uint8_t> Div255(const Vec<N, uint16_t>& x) {
  return Cast<uint8_t>((x + 127) / 255);
}

// ApproxScale(x,y) approximates Div255(Cast<uint16_t>(x)*Cast<uint16_t>(y)) within a bit, and is
// always perfect when x or y is 0 or 255.
SIN Vec<N, uint8_t> ApproxScale(const Vec<N, uint8_t>& x, const Vec<N, uint8_t>& y) {
  // All of (x*y+x)/256, (x*y+y)/256, and (x*y+255)/256 meet the criteria above. We happen to have
  // historically picked (x*y+x)/256.
  auto X = Cast<uint16_t>(x), Y = Cast<uint16_t>(y);
  return Cast<uint8_t>((X * Y + X) / 256);
}

// SaturatedAdd(x,y) sums values and clamps to the maximum value instead of overflowing.
SINT std::enable_if_t<std::is_unsigned_v<T>, Vec<N, T>> SaturatedAdd(const Vec<N, T>& x,
                                                                     const Vec<N, T>& y) {
#if TGFX_USE_SIMD && (TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1 || \
                      defined(TGFX_ARM_HAS_NEON) || TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX)
  // Both SSE and ARM have 16-lane saturated adds, so use intrinsics for those and recurse down
  // or Join up to take advantage.
  if constexpr (N == 16 && sizeof(T) == 1) {
#if TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1
    return BitCast<Vec<N, T>>(_mm_adds_epu8(BitCast<__m128i>(x), BitCast<__m128i>(y)));
#elif TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
    return BitCast<Vec<N, T>>(__lsx_vsadd_bu(BitCast<__m128i>(x), BitCast<__m128i>(y)));
#else  // TGFX_ARM_HAS_NEON
    return BitCast<Vec<N, T>>(vqaddq_u8(BitCast<uint8x16_t>(x), BitCast<uint8x16_t>(y)));
#endif
  } else if constexpr (N < 16 && sizeof(T) == 1) {
    return SaturatedAdd(Join(x, x), Join(y, y)).lo;
  } else if constexpr (sizeof(T) == 1) {
    return Join(SaturatedAdd(x.lo, y.lo), SaturatedAdd(x.hi, y.hi));
  }
#endif
  // Otherwise saturate manually
  auto sum = x + y;
  return IfThenElse(sum < x, Vec<N, T>(std::numeric_limits<T>::max()), sum);
}

/**
  * The ScaledDividerU32 takes a divisor > 1, and creates a function divide(numerator) that
  * calculates a numerator / denominator. For this to be rounded properly, numerator should have
  * half added in:
  *   divide(numerator + half) == floor(numerator/denominator + 1/2).
  *
  *   This gives an answer within +/- 1 from the true value.
  *
  *   Derivation of half:
  *     numerator/denominator + 1/2 = (numerator + half) / d
  *     numerator + denominator / 2 = numerator + half
  *     half = denominator / 2.
  *
  *    Because half is divided by 2, that division must also be rounded.
  *    half == denominator / 2 = (denominator + 1) / 2.
  *
  *    The divisorFactor is just a scaled value:
  *     divisorFactor = (1 / divisor) * 2 ^ 32.
  *    The maximum that can be divided and rounded is UINT_MAX - half.
  */
class ScaledDividerU32 {
 public:
  explicit ScaledDividerU32(uint32_t divisor)
      : fDivisorFactor{(uint32_t)(std::round((1.0 / divisor) * (1ull << 32)))}, fHalf{
                                                                                    (divisor + 1) >>
                                                                                    1} {
    assert(divisor > 1);
  }

  Vec<4, uint32_t> divide(const Vec<4, uint32_t>& numerator) const {
#if TGFX_USE_SIMD && defined(TGFX_ARM_HAS_NEON)
    uint64x2_t hi = vmull_n_u32(vget_high_u32(ToVext(numerator)), fDivisorFactor);
    uint64x2_t lo = vmull_n_u32(vget_low_u32(ToVext(numerator)), fDivisorFactor);

    return ToVec<4, uint32_t>(vcombine_u32(vshrn_n_u64(lo, 32), vshrn_n_u64(hi, 32)));
#else
    return Cast<uint32_t>((Cast<uint64_t>(numerator) * fDivisorFactor) >> 32);
#endif
  }

  uint32_t half() const {
    return fHalf;
  }
  uint32_t divisorFactor() const {
    return fDivisorFactor;
  }

 private:
  const uint32_t fDivisorFactor;
  const uint32_t fHalf;
};

SIN Vec<N, uint16_t> Mull(const Vec<N, uint8_t>& x, const Vec<N, uint8_t>& y) {
#if TGFX_USE_SIMD && defined(TGFX_ARM_HAS_NEON)
  // With NEON we can do eight u8*u8 -> u16 in one instruction, vMull_u8 (read, mul-long).
  if constexpr (N == 8) {
    return ToVec<8, uint16_t>(vMull_u8(ToVext(x), ToVext(y)));
  } else if constexpr (N < 8) {
    return Mull(Join(x, x), Join(y, y)).lo;
  } else {  // N > 8
    return Join(Mull(x.lo, y.lo), Mull(x.hi, y.hi));
  }
#else
  return Cast<uint16_t>(x) * Cast<uint16_t>(y);
#endif
}

SIN Vec<N, uint32_t> Mull(const Vec<N, uint16_t>& x, const Vec<N, uint16_t>& y) {
#if TGFX_USE_SIMD && defined(TGFX_ARM_HAS_NEON)
  // NEON can do four u16*u16 -> u32 in one instruction, vMull_u16
  if constexpr (N == 4) {
    return ToVec<4, uint32_t>(vMull_u16(ToVext(x), ToVext(y)));
  } else if constexpr (N < 4) {
    return Mull(Join(x, x), Join(y, y)).lo;
  } else {  // N > 4
    return Join(Mull(x.lo, y.lo), Mull(x.hi, y.hi));
  }
#else
  return Cast<uint32_t>(x) * Cast<uint32_t>(y);
#endif
}

SIN Vec<N, uint16_t> Mulhi(const Vec<N, uint16_t>& x, const Vec<N, uint16_t>& y) {
#if TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1
  // Use _mm_Mulhi_epu16 for 8xuint16_t and Join or split to get there.
  if constexpr (N == 8) {
    return BitCast<Vec<8, uint16_t>>(_mm_Mulhi_epu16(BitCast<__m128i>(x), BitCast<__m128i>(y)));
  } else if constexpr (N < 8) {
    return Mulhi(Join(x, x), Join(y, y)).lo;
  } else {  // N > 8
    return Join(Mulhi(x.lo, y.lo), Mulhi(x.hi, y.hi));
  }
#elif TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
  if constexpr (N == 8) {
    return BitCast<Vec<8, uint16_t>>(__lsx_vmuh_hu(BitCast<__m128i>(x), BitCast<__m128i>(y)));
  } else if constexpr (N < 8) {
    return Mulhi(Join(x, x), Join(y, y)).lo;
  } else {  // N > 8
    return Join(Mulhi(x.lo, y.lo), Mulhi(x.hi, y.hi));
  }
#else
  return Cast<uint16_t>(Mull(x, y) >> 16);
#endif
}

SINT T Dot(const Vec<N, T>& a, const Vec<N, T>& b) {
  // While Dot is a "horizontal" operation like any or all, it needs to remain in floating point and
  // there aren't really any good SIMD instructions that make it faster. The constexpr cases remove
  // the for loop in the only cases we realistically call.
  auto ab = a * b;
  if constexpr (N == 2) {
    return ab[0] + ab[1];
  } else if constexpr (N == 4) {
    return ab[0] + ab[1] + ab[2] + ab[3];
  } else {
    T sum = ab[0];
    for (int i = 1; i < N; ++i) {
      sum += ab[i];
    }
    return sum;
  }
}

SIT T Cross(const Vec<2, T>& a, const Vec<2, T>& b) {
  auto x = a * Shuffle<1, 0>(b);
  return x[0] - x[1];
}

SIN float Length(const Vec<N, float>& v) {
  return std::sqrt(Dot(v, v));
}

SIN double Length(const Vec<N, double>& v) {
  return std::sqrt(Dot(v, v));
}

SIN Vec<N, float> Normalize(const Vec<N, float>& v) {
  return v / Length(v);
}

SIN Vec<N, double> Normalize(const Vec<N, double>& v) {
  return v / Length(v);
}

// Subtracting a value from itself will result in zero, except for NAN or ±Inf, which make NAN.
// Multiplying a group of values against zero will result in zero for each product, except for
// NAN or ±Inf, which will result in NAN and continue resulting in NAN for the rest of the elements.
// This generates better code than `std::isfinite` when building with clang-cl.
template <typename T, typename... Pack, std::enable_if_t<std::is_floating_point_v<T>, bool> = true>
static inline bool IsFinite(T x, Pack... values) {
  T prod = x - x;
  prod = (prod * ... * values);
  // At this point, `prod` will either be NaN or 0.
  return prod == prod;
}

template <typename T, std::enable_if_t<std::is_floating_point_v<T>, bool> = true>
static inline bool IsFinite(const T array[], int count) {
  T x = array[0];
  T prod = x - x;
  for (int i = 1; i < count; ++i) {
    prod *= array[i];
  }
  // At this point, `prod` will either be NaN or 0.
  return prod == prod;
}

SINT bool Isfinite(const Vec<N, T>& v) {
  // Multiply all values together with 0. If they were all finite, the output is 0 (also finite). If
  // any were not, we'll get nan.
  return IsFinite(Dot(v, Vec<N, T>(0)));
}

// De-interleaving load of 4 vectors.
//
// WARNING: These are really only supported well on NEON. Consider restructuring your data before
// resorting to these methods.
SIT void StridedLoad4(const T* v, Vec<1, T>& a, Vec<1, T>& b, Vec<1, T>& c, Vec<1, T>& d) {
  a.val = v[0];
  b.val = v[1];
  c.val = v[2];
  d.val = v[3];
}
SINT void StridedLoad4(const T* v, Vec<N, T>& a, Vec<N, T>& b, Vec<N, T>& c, Vec<N, T>& d) {
  StridedLoad4(v, a.lo, b.lo, c.lo, d.lo);
  StridedLoad4(v + 4 * (N / 2), a.hi, b.hi, c.hi, d.hi);
}
#if TGFX_USE_SIMD && defined(TGFX_ARM_HAS_NEON)
#define IMPL_LOAD4_TRANSPOSED(N, T, VLD)                                                     \
  SI void StridedLoad4(const T* v, Vec<N, T>& a, Vec<N, T>& b, Vec<N, T>& c, Vec<N, T>& d) { \
    auto mat = VLD(v);                                                                       \
    a = BitCast<Vec<N, T>>(mat.val[0]);                                                      \
    b = BitCast<Vec<N, T>>(mat.val[1]);                                                      \
    c = BitCast<Vec<N, T>>(mat.val[2]);                                                      \
    d = BitCast<Vec<N, T>>(mat.val[3]);                                                      \
  }
IMPL_LOAD4_TRANSPOSED(2, uint32_t, vld4_u32)
IMPL_LOAD4_TRANSPOSED(4, uint16_t, vld4_u16)
IMPL_LOAD4_TRANSPOSED(8, uint8_t, vld4_u8)
IMPL_LOAD4_TRANSPOSED(2, int32_t, vld4_s32)
IMPL_LOAD4_TRANSPOSED(4, int16_t, vld4_s16)
IMPL_LOAD4_TRANSPOSED(8, int8_t, vld4_s8)
IMPL_LOAD4_TRANSPOSED(2, float, vld4_f32)
IMPL_LOAD4_TRANSPOSED(4, uint32_t, vld4q_u32)
IMPL_LOAD4_TRANSPOSED(8, uint16_t, vld4q_u16)
IMPL_LOAD4_TRANSPOSED(16, uint8_t, vld4q_u8)
IMPL_LOAD4_TRANSPOSED(4, int32_t, vld4q_s32)
IMPL_LOAD4_TRANSPOSED(8, int16_t, vld4q_s16)
IMPL_LOAD4_TRANSPOSED(16, int8_t, vld4q_s8)
IMPL_LOAD4_TRANSPOSED(4, float, vld4q_f32)
#undef IMPL_LOAD4_TRANSPOSED

#elif TGFX_USE_SIMD && TGFX_CPU_SSE_LEVEL >= TGFX_CPU_SSE_LEVEL_SSE1

SI void StridedLoad4(const float* v, Vec<4, float>& a, Vec<4, float>& b, Vec<4, float>& c,
                     Vec<4, float>& d) {
  __m128 a_ = _mm_loadu_ps(v);
  __m128 b_ = _mm_loadu_ps(v + 4);
  __m128 c_ = _mm_loadu_ps(v + 8);
  __m128 d_ = _mm_loadu_ps(v + 12);
  _MM_TRANSPOSE4_PS(a_, b_, c_, d_);
  a = BitCast<Vec<4, float>>(a_);
  b = BitCast<Vec<4, float>>(b_);
  c = BitCast<Vec<4, float>>(c_);
  d = BitCast<Vec<4, float>>(d_);
}

#elif TGFX_USE_SIMD && TGFX_CPU_LSX_LEVEL >= TGFX_CPU_LSX_LEVEL_LSX
#define _LSX_TRANSPOSE4(row0, row1, row2, row3) \
  do {                                          \
    __m128i __t0 = __lsx_vilvl_w(row1, row0);   \
    __m128i __t1 = __lsx_vilvl_w(row3, row2);   \
    __m128i __t2 = __lsx_vilvh_w(row1, row0);   \
    __m128i __t3 = __lsx_vilvh_w(row3, row2);   \
    (row0) = __lsx_vilvl_d(__t1, __t0);         \
    (row1) = __lsx_vilvh_d(__t1, __t0);         \
    (row2) = __lsx_vilvl_d(__t3, __t2);         \
    (row3) = __lsx_vilvh_d(__t3, __t2);         \
  } while (0)

SI void StridedLoad4(const int* v, Vec<4, int>& a, Vec<4, int>& b, Vec<4, int>& c, Vec<4, int>& d) {
  __m128i a_ = __lsx_vld(v, 0);
  __m128i b_ = __lsx_vld(v, 16);
  __m128i c_ = __lsx_vld(v, 32);
  __m128i d_ = __lsx_vld(v, 48);
  _LSX_TRANSPOSE4(a_, b_, c_, d_);
  a = BitCast<Vec<4, int>>(a_);
  b = BitCast<Vec<4, int>>(b_);
  c = BitCast<Vec<4, int>>(c_);
  d = BitCast<Vec<4, int>>(d_);
}
#endif

// De-interleaving load of 2 vectors.
//
// WARNING: These are really only supported well on NEON. Consider restructuring your data before
// resorting to these methods.
SIT void StridedLoad2(const T* v, Vec<1, T>& a, Vec<1, T>& b) {
  a.val = v[0];
  b.val = v[1];
}
SINT void StridedLoad2(const T* v, Vec<N, T>& a, Vec<N, T>& b) {
  StridedLoad2(v, a.lo, b.lo);
  StridedLoad2(v + 2 * (N / 2), a.hi, b.hi);
}
#if TGFX_USE_SIMD && defined(TGFX_ARM_HAS_NEON)
#define IMPL_LOAD2_TRANSPOSED(N, T, VLD)                         \
  SI void StridedLoad2(const T* v, Vec<N, T>& a, Vec<N, T>& b) { \
    auto mat = VLD(v);                                           \
    a = BitCast<Vec<N, T>>(mat.val[0]);                          \
    b = BitCast<Vec<N, T>>(mat.val[1]);                          \
  }
IMPL_LOAD2_TRANSPOSED(2, uint32_t, vld2_u32)
IMPL_LOAD2_TRANSPOSED(4, uint16_t, vld2_u16)
IMPL_LOAD2_TRANSPOSED(8, uint8_t, vld2_u8)
IMPL_LOAD2_TRANSPOSED(2, int32_t, vld2_s32)
IMPL_LOAD2_TRANSPOSED(4, int16_t, vld2_s16)
IMPL_LOAD2_TRANSPOSED(8, int8_t, vld2_s8)
IMPL_LOAD2_TRANSPOSED(2, float, vld2_f32)
IMPL_LOAD2_TRANSPOSED(4, uint32_t, vld2q_u32)
IMPL_LOAD2_TRANSPOSED(8, uint16_t, vld2q_u16)
IMPL_LOAD2_TRANSPOSED(16, uint8_t, vld2q_u8)
IMPL_LOAD2_TRANSPOSED(4, int32_t, vld2q_s32)
IMPL_LOAD2_TRANSPOSED(8, int16_t, vld2q_s16)
IMPL_LOAD2_TRANSPOSED(16, int8_t, vld2q_s8)
IMPL_LOAD2_TRANSPOSED(4, float, vld2q_f32)
#undef IMPL_LOAD2_TRANSPOSED
#endif

// Define commonly used aliases
using float2 = Vec<2, float>;
using float4 = Vec<4, float>;
using float8 = Vec<8, float>;

using double2 = Vec<2, double>;
using double4 = Vec<4, double>;
using double8 = Vec<8, double>;

using byte2 = Vec<2, uint8_t>;
using byte4 = Vec<4, uint8_t>;
using byte8 = Vec<8, uint8_t>;
using byte16 = Vec<16, uint8_t>;

using int2 = Vec<2, int32_t>;
using int4 = Vec<4, int32_t>;
using int8 = Vec<8, int32_t>;

using ushort2 = Vec<2, uint16_t>;
using ushort4 = Vec<4, uint16_t>;
using ushort8 = Vec<8, uint16_t>;

using uint2 = Vec<2, uint32_t>;
using uint4 = Vec<4, uint32_t>;
using uint8 = Vec<8, uint32_t>;

using long2 = Vec<2, int64_t>;
using long4 = Vec<4, int64_t>;
using long8 = Vec<8, int64_t>;

// Use with from_half and to_half to convert between floatX, and use these for storage.
using half2 = Vec<2, uint16_t>;
using half4 = Vec<4, uint16_t>;
using half8 = Vec<8, uint16_t>;

}  // namespace tgfx

#undef SINTU
#undef SINT
#undef SIN
#undef SIT
#undef SI
#undef TGFX_ALWAYS_INLINE
#undef TGFX_USE_SIMD
