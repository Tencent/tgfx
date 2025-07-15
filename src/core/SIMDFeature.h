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

#if !defined(TGFX_BUILD_FOR_ANDROID) && !defined(TGFX_BUILD_FOR_IOS) && \
    !defined(TGFX_BUILD_FOR_WIN) && !defined(TGFX_BUILD_FOR_UNIX) && !defined(TGFX_BUILD_FOR_MAC)

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(_WIN32) || defined(__SYMBIAN32__)
#define TGFX_BUILD_FOR_WIN
#elif defined(ANDROID) || defined(__ANDROID__)
#define TGFX_BUILD_FOR_ANDROID
#elif defined(linux) || defined(__linux) || defined(__FreeBSD__) || defined(__OpenBSD__) ||    \
    defined(__sun) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__Fuchsia__) || \
    defined(__GLIBC__) || defined(__GNU__) || defined(__unix__)
#define TGFX_BUILD_FOR_UNIX
#elif TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
#define TGFX_BUILD_FOR_IOS
#else
#define TGFX_BUILD_FOR_MAC
#endif
#endif  // end TGFX_BUILD_FOR_*

/**
 *  TGFX_CPU_SSE_LEVEL
 *
 *  If defined, TGFX_CPU_SSE_LEVEL should be set to the highest supported level. On non-intel CPU
 *  this should be undefined.
 */
#define TGFX_CPU_SSE_LEVEL_SSE1 10
#define TGFX_CPU_SSE_LEVEL_SSE2 20
#define TGFX_CPU_SSE_LEVEL_SSE3 30
#define TGFX_CPU_SSE_LEVEL_SSSE3 31
#define TGFX_CPU_SSE_LEVEL_SSE41 41
#define TGFX_CPU_SSE_LEVEL_SSE42 42
#define TGFX_CPU_SSE_LEVEL_AVX 51
#define TGFX_CPU_SSE_LEVEL_AVX2 52
#define TGFX_CPU_SSE_LEVEL_TGFXX 60

/**
 *  TGFX_CPU_LSX_LEVEL
 *
 *  If defined, TGFX_CPU_LSX_LEVEL should be set to the highest supported level. On non-loongarch
 *  CPU this should be undefined.
 */
#define TGFX_CPU_LSX_LEVEL_LSX 70
#define TGFX_CPU_LSX_LEVEL_LASX 80

// Are we in GCC/Clang?
#ifndef TGFX_CPU_SSE_LEVEL
// These checks must be done in descending order to ensure we set the highest available SSE level.
#if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
    defined(__AVX512BW__) && defined(__AVX512VL__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_TGFXX
#elif defined(__AVX2__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_AVX2
#elif defined(__AVX__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_AVX
#elif defined(__SSE4_2__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSE42
#elif defined(__SSE4_1__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSE41
#elif defined(__SSSE3__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSSE3
#elif defined(__SSE3__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSE3
#elif defined(__SSE2__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSE2
#endif
#endif

#ifndef TGFX_CPU_LSX_LEVEL
#if defined(__loongarch_asx)
#define TGFX_CPU_LSX_LEVEL TGFX_CPU_LSX_LEVEL_LASX
#elif defined(__loongarch_sx)
#define TGFX_CPU_LSX_LEVEL TGFX_CPU_LSX_LEVEL_LSX
#endif
#endif

// Are we in VisualStudio?
#ifndef TGFX_CPU_SSE_LEVEL
// These checks must be done in descending order to ensure we set the highest available SSE level.
// 64-bit intel guarantees at least SSE2 support.
#if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
    defined(__AVX512BW__) && defined(__AVX512VL__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_TGFXX
#elif defined(__AVX2__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_AVX2
#elif defined(__AVX__)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_AVX
#elif defined(_M_X64) || defined(_M_AMD64)
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSE2
#elif defined(_M_IX86_FP)
#if _M_IX86_FP >= 2
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSE2
#elif _M_IX86_FP == 1
#define TGFX_CPU_SSE_LEVEL TGFX_CPU_SSE_LEVEL_SSE1
#endif
#endif
#endif

// ARM defines
#if defined(__arm__) && (!defined(__APPLE__) || !TARGET_IPHONE_SIMULATOR)
#define TGFX_CPU_ARM32
#elif defined(__aarch64__)
#define TGFX_CPU_ARM64
#endif

// All 64-bit ARM chips have NEON.  Many 32-bit ARM chips do too.
#if !defined(TGFX_ARM_HAS_NEON) && defined(__ARM_NEON)
#define TGFX_ARM_HAS_NEON
#endif