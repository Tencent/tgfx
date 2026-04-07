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

#pragma once

#include <cstddef>
#include <cstdint>
#include "Socket.h"

namespace tgfx::inspect {

static constexpr size_t Sha256DigestSize = 32;
static constexpr size_t Sha256BlockSize = 64;
static constexpr size_t ChallengeSize = 16;
static constexpr size_t HmacSize = Sha256DigestSize;

void sha256(const uint8_t* data, size_t len, uint8_t out[Sha256DigestSize]);

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen,
                uint8_t out[HmacSize]);

bool generateChallenge(uint8_t out[ChallengeSize]);

/**
 * Performs challenge-response authentication on an accepted socket. The server sends a random
 * challenge, reads back an HMAC-SHA256 response, and verifies it against the shared key.
 * Returns true if the client is authenticated.
 */
bool authenticateClient(Socket* sock);

}  // namespace tgfx::inspect
