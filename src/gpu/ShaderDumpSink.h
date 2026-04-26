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
#include "tgfx/core/BytesKey.h"

namespace tgfx {
class ProgramInfo;

/**
 * Runtime shader collection sink. When the environment variable TGFX_SHADER_DUMP_DIR is set to a
 * writable directory path, each unique program produced by ProgramBuilder::CreateProgram is
 * persisted to:
 *   <dir>/shaders/<keyHex>.vert.glsl
 *   <dir>/shaders/<keyHex>.frag.glsl
 *   <dir>/programs.jsonl  (one JSON object per line: key, files, processor-tuple metadata)
 *
 * The sink is deduplicating by <keyHex>. Writing is best-effort and failures are silently ignored
 * so draw traffic is never blocked. This is intentionally a runtime observation tool: it does not
 * modify the produced program or the shader text.
 */
class ShaderDumpSink {
 public:
  /**
   * Returns true if TGFX_SHADER_DUMP_DIR is set (dumping enabled). Cached after first call.
   */
  static bool Enabled();

  /**
   * Records a finalized program. No-op when Enabled() is false. Thread-safe.
   *
   * @param programKey  The GlobalCache key — encoded as hex in the output filenames.
   * @param programInfo The pipeline descriptor (used to emit the processor-tuple metadata).
   * @param vertexText  The finalized VS source (as would be handed to the GPU driver).
   * @param fragmentText The finalized FS source.
   */
  static void Record(const BytesKey& programKey, const ProgramInfo* programInfo,
                     const std::string& vertexText, const std::string& fragmentText);
};
}  // namespace tgfx
