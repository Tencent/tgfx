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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Shared assertions for the verify-rule tests. Every rule returns a violation count and reports
// one diagnostic per violation; the build gates on the ledger alone, so the pairing is what
// keeps the returned count honest — a rule that counted without reporting (or the reverse)
// would trip the gate with a summary that does not mention the failure. Tests pin it by
// comparing the returned count with the failing records the call added.

#pragma once

#include <cstddef>
#include "core/Diagnostics.h"

namespace tgfx {

/// The records in a ledger that fail a build (Error or Unverifiable severities).
inline size_t FailuresIn(const Diagnostics& diagnostics) {
  size_t failures = 0;
  for (const auto& record : diagnostics.records()) {
    if (record.severity == Severity::Error || record.severity == Severity::Unverifiable) {
      failures++;
    }
  }
  return failures;
}

/// The failing records a ledger gained since recordsBefore (snapshot records().size() first).
inline size_t FailuresSince(const Diagnostics& diagnostics, size_t recordsBefore) {
  size_t failures = 0;
  const auto& records = diagnostics.records();
  for (size_t i = recordsBefore; i < records.size(); i++) {
    if (records[i].severity == Severity::Error || records[i].severity == Severity::Unverifiable) {
      failures++;
    }
  }
  return failures;
}

}  // namespace tgfx
