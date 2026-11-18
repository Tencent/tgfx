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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The plan: one assembled chain fragment's interface and call sequence, backend-neutral. The
// parameters live as named members past the variant declaration prefix — each with the slot it
// belongs to, so the writer reads the typed slot field directly and the positional pool never
// enters the picture.

#pragma once

#include <set>
#include <string>
#include <vector>
#include "gpu/shaders/contract/ChainComposition.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {

/// One call in the straight-line body: an operator's function with marshalled arguments (input
/// expressions, named parameter members, literals) writing one local.
struct PlanCall {
  int op = 0;
  int slot = -1;
  std::vector<std::string> args;
  std::string out;
  /// The wrapper local this call's value sinks into after it runs: "result", "clipRegister" or
  /// "coverageRootRegister" (empty when the call feeds later calls only).
  std::string sink;
  /// A trailing statement printed after the call (the blend's input-alpha multiply).
  std::string post;
};

/// One named parameter member: the std140 offset is relative to the parameter block's start, and
/// the (op, slot, param) triple names the typed slot field the writer reads.
struct PlanParam {
  std::string member;
  int op = 0;
  int slot = -1;
  const char* param = "";
  FragmentType type = FragmentType::Float4;
  uint32_t offset = 0;  // bytes from the parameter block's start (std140 rules)
  uint32_t size = 0;    // the member's byte size
};

/// The assembled fragment's own facts. The parameter block appends past the variant declaration
/// prefix (the prefix keeps the entry-level members the existing writers upload); the per-slot
/// parameters live in it as named members.
struct ChainPlan {
  std::vector<PlanCall> calls;
  std::set<int> usedOps;
  std::vector<PlanParam> params;
  uint32_t paramBlockSize = 0;
  bool hasTexture = false;
  /// The varying contract: the leaf coordinate varyings the composition reads and whether the
  /// resource coordinates (gradient/noise) are read. Both stages declare exactly this set.
  std::set<int> leafVaryings;
  bool needsSourceCoords = false;
  /// The xfer's advanced blend pass: the epilogue reads result and coverageRootRegister through
  /// the xp source function and the XPBlendMode member.
  bool xpNeedsBlendPass = false;
};

/// Builds the plan from a composition: every slot becomes one call whose parameter members are
/// declared as they are marshalled (the interface is the emission's closure). Returns an empty
/// plan (no calls) when an operator or input is not representable.
ChainPlan BuildChainPlan(const ChainComposition& composition);

}  // namespace tgfx
