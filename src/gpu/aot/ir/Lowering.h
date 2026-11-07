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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>

namespace tgfx {

class FragmentProcessor;
class AOTEffectGraph;

/**
 * Lowers one processor chain into its effect graph: a geometry-color root plus one node per
 * processor, in chain order. This is the only place a processor tree becomes IR, so the lowering
 * lives with the IR it builds rather than with the planner that consumes it.
 *
 * @param processors         The chain, root-most producer last (each processor takes the previous
 *                           node as its input).
 * @param graph              Receives the finished graph on success.
 * @param blockingProcessor  When not null, receives the class name of the first processor that has
 *                           no AOT lowering; cleared on entry.
 * @return True when every processor lowered and the graph finished.
 */
bool LowerToGraph(const std::vector<const FragmentProcessor*>& processors, AOTEffectGraph* graph,
                  std::string* blockingProcessor = nullptr);

}  // namespace tgfx
