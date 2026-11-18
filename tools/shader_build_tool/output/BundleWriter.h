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

#include <string>
#include <vector>
#include "core/Diagnostics.h"
#include "core/Types.h"

namespace tgfx {

/// Writes the shader bundle file with separate vertex and fragment pools. Each entry is the
/// variant id and its compiled bytes; the interface is not stored, because the runtime derives it
/// from the declaration table the compile step has already checked the reflection against.
/// The composed payloads join the fragment pool under their composed ids (the high bit set): they
/// are specialized, pre-spliced texts the runtime looks up by structure key, reusing the pool's
/// storage and every validation unchanged.
/// When compress is true, the data pool is zstd-compressed (compressionType=2 in header).
/// Failures are reported to diagnostics (output.write-failed, or output.duplicate-key when two
/// variants collide on one stage key with different content).
bool WriteBundle(const std::string& outPath, const std::string& profileTag,
                 const std::vector<VariantData>& variants,
                 const std::vector<ComposedPayload>& composed, bool compress,
                 Diagnostics* diagnostics);

}  // namespace tgfx
