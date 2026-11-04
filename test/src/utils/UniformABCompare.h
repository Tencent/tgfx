/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include <string>

namespace tgfx {

class Context;

/**
 * The S4 upload-unification gate (plan S1.2): runs one scene on the precompiled upload route and
 * pins its per-draw TGFX_UNIF_DUMP trace byte-for-byte against the golden under
 * test/baseline/uniform/<name>.txt.
 *
 * The trace is normalized to what the refactor must preserve: every dump header (it carries the
 * program key, whose ClassIDs are link-order noise, and the writer's route/suffix tags) becomes a
 * positional [DRAW] marker, while the field names, the values in offset order and the sampler
 * bindings stay verbatim — a draw one route drops shows up as a shifted sequence instead of an
 * invisible hole.
 *
 * The invariant S4 must hold at every step: migrating a module from name-based writes to
 * offset-based writes changes the trace, so the golden pins the current writer behavior. Goldens
 * whose file still carries both route sections are compared on their AOT section alone.
 *
 * `draw` must only issue the draw calls; the harness clears the program cache, flushes and
 * submits once per run, and requires the trace to be run-to-run stable before comparing.
 * Returns an empty string on success, otherwise a human-readable diff. Set TGFX_ACCEPT_BASELINE
 * to (re)write the golden.
 */
std::string PinUniformTrace(Context* context, const std::string& name,
                            const std::function<void()>& draw);

}  // namespace tgfx
