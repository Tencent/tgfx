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
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

/// How a diagnostic affects the build.
enum class Severity {
  /// The build must fail and must not write its bundles.
  Error,
  /// Reported and counted, but does not fail the build.
  Warning,
  /// A check could not run at all. Distinct from Error on purpose: "checked and failed" and
  /// "never checked" demand different responses, and a silent skip is what this distinction
  /// exists to prevent. Fails the build like an Error.
  Unverifiable,
};

/// The sublayer that produced the diagnostic; the first segment of every code.
enum class Layer { Sources, Plan, Compile, Emit, Verify, Output, Cli };

/// One diagnostic record: what happened, to what, and where.
struct Diagnostic {
  Severity severity = Severity::Error;
  Layer layer = Layer::Cli;
  /// A DiagnosticCode constant (core/DiagnosticCodes.h); never a temporary string.
  const char* code = nullptr;
  /// The shader family the record is about; empty when the record is not about one.
  std::string shader;
  /// The profile tag; empty means "every registered profile" (a common failure).
  std::string profile;
  int vertPermutationIndex = -1;
  int fragPermutationIndex = -1;
  /// The human text, already rendered with its expected/actual values.
  std::string message;
};

/// The error ledger of one build, and the log of everything reported through it. A compile
/// failure either affects every requested profile (a stage that never compiles is dead for all
/// backends) or exactly one profile (a translation only that backend runs), and the build report
/// needs both figures per profile. A profile takes part in the ledger when it is registered, an
/// error recorded for an unregistered profile still creates its entry, and a common error only
/// lands on the profiles registered at that moment — the report's figures have always had that
/// point-in-time semantics, and the derived counts keep it.
class Diagnostics {
 public:
  /// Adds profileTag to the ledger with a count of zero. A tag that is already present keeps
  /// its count, so re-registering cannot reset the running figures.
  void registerProfile(const std::string& profileTag);

  /// Records one diagnostic: appends it to the log and lands it on the ledger (Error and
  /// Unverifiable only, on every registered profile when its profile is empty).
  void report(Diagnostic diagnostic);

  /// The per-profile error counts, keyed by profile tag in tag order. Error and Unverifiable
  /// records count here (both fail the build); warnings do not.
  const std::map<std::string, uint64_t>& errorCounts() const;

  /// The log, in report order.
  const std::vector<Diagnostic>& records() const {
    return recordLog;
  }

  /// The number of records of one severity.
  size_t count(Severity severity) const;

  /// True when at least one record would fail the build (Error or Unverifiable).
  bool hasErrors() const;

 private:
  std::map<std::string, uint64_t> profileErrorCounts_;
  std::vector<Diagnostic> recordLog;
};

}  // namespace tgfx
