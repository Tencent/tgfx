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

#include "pdf/MD5.h"
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(_WIN32)
// clang-format off
#include <windows.h>
#include <wincrypt.h>
// clang-format on
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(ANDROID)
#include <openssl/md5.h>
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include "algorithm/md5.hpp"
#pragma clang diagnostic pop
#endif

namespace tgfx {

MD5::Digest MD5::Calculate(const void* bytes, size_t size) {
  Digest digest = {};
  if (bytes == nullptr || size == 0) {
    return digest;
  }
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  CC_MD5(bytes, static_cast<CC_LONG>(size), digest.data());
#pragma clang diagnostic pop
#elif defined(_WIN32)
  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;
  if (CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
    if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
      if (CryptHashData(hHash, static_cast<const BYTE*>(bytes), static_cast<DWORD>(size), 0)) {
        DWORD hashLen = static_cast<DWORD>(digest.size());
        CryptGetHashParam(hHash, HP_HASHVAL, digest.data(), &hashLen, 0);
      }
      CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
  }
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(ANDROID)
  ::MD5(static_cast<const unsigned char*>(bytes), size, digest.data());
#else
  // Fallback to digestpp for other platforms
  digestpp::md5 hasher;
  hasher.absorb(static_cast<const unsigned char*>(bytes), size);
  hasher.digest(digest.data(), digest.size());
#endif
  return digest;
}

}  // namespace tgfx