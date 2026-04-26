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
struct ShaderTextCapture;

/**
 * Runtime shader collection sink. When the environment variable TGFX_SHADER_DUMP_DIR is set to a
 * writable directory path, each unique program produced by ProgramBuilder::CreateProgram is
 * persisted to:
 *   <dir>/shaders/<keyHex>.vert.glsl
 *   <dir>/shaders/<keyHex>.frag.glsl
 *   <dir>/programs.jsonl  (one JSON object per line: key, files, processor-tuple metadata,
 *                          and uniform/sampler/attribute layouts)
 *
 * The sink is deduplicating by <keyHex>. Writing is best-effort: file write failures are silently
 * ignored and the JSONL line is only appended once both shader files are on disk, so the index
 * never references missing files. This is intentionally a runtime observation tool: it does not
 * modify the produced program or the shader text.
 *
 * IMPORTANT: <keyHex> is NOT stable across process invocations. It encodes Processor::classID
 * values, which are assigned via UniqueID::Next() at first use and therefore depend on process
 * initialization order. The shader text itself IS stable per pipeline structure, so offline
 * consumers that need a cross-process logical identifier should hash the shader text (or use the
 * tuple metadata) instead of keyHex.
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
   * @param capture     The shader text and layout captured by ProgramBuilder::CreateProgram.
   */
  static void Record(const BytesKey& programKey, const ProgramInfo* programInfo,
                     const ShaderTextCapture& capture);
};
}  // namespace tgfx
