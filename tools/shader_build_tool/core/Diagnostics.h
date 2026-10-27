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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "as IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace tgfx {

// The core of the tool's module layering. Everything here is what every other module may use
// and depends on nothing but the standard library, so the tool keeps a single bottom:
//
//   cli ─▶ plan ─▶ compile ─▶ emit ─▶ verify ─▶ output
//     │      │        │       │       │         │
//     └──────┴────────┴───────┴───────┴─────────┴─▶ core
//
// Modules of one row never include each other; LayeringTest reads the include lines and fails
// when that is lost, which is what lets one pass be refactored without touching another.

/// The error ledger of one build. A compile failure either affects every requested profile (a
/// stage that never compiles is dead for all backends) or exactly one profile (a translation
/// only that backend runs), and the build report needs both figures per profile. A profile
/// takes part in the ledger when it is registered, and an error recorded for an unregistered
/// profile still creates its entry, so nothing is silently dropped.
class Diagnostics {
 public:
  /// Adds profileTag to the ledger with a count of zero. A tag that is already present keeps
  /// its count, so re-registering cannot reset the running figures.
  void registerProfile(const std::string& profileTag);

  /// Counts one error against every registered profile.
  void recordCommonError();

  /// Counts one error against profileTag, creating its entry when it is not registered.
  void recordProfileError(const std::string& profileTag);

  /// The per-profile error counts, keyed by profile tag in tag order.
  const std::map<std::string, uint64_t>& errorCounts() const;

 private:
  std::map<std::string, uint64_t> profileErrorCounts_;
};

}  // namespace tgfx
