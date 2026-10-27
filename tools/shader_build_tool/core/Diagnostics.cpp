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

#include "core/Diagnostics.h"
#include "core/DiagnosticCodes.h"

namespace tgfx {

void Diagnostics::registerProfile(const std::string& profileTag) {
  profileErrorCounts_.emplace(profileTag, 0);
}

void Diagnostics::report(Diagnostic diagnostic) {
  // The ledger figures are updated here, not derived on read: whether a common error lands on a
  // profile depends on whether that profile was registered at report time, and the report has
  // always had that semantics. Keeping the mutation at report time keeps the figures identical.
  if (diagnostic.severity == Severity::Error || diagnostic.severity == Severity::Unverifiable) {
    if (diagnostic.profile.empty()) {
      for (auto& profile : profileErrorCounts_) {
        profile.second++;
      }
    } else {
      profileErrorCounts_[diagnostic.profile]++;
    }
  }
  recordLog.push_back(std::move(diagnostic));
}

const std::map<std::string, uint64_t>& Diagnostics::errorCounts() const {
  return profileErrorCounts_;
}

size_t Diagnostics::count(Severity severity) const {
  size_t total = 0;
  for (const auto& record : recordLog) {
    if (record.severity == severity) {
      total++;
    }
  }
  return total;
}

bool Diagnostics::hasErrors() const {
  return count(Severity::Error) > 0 || count(Severity::Unverifiable) > 0;
}

}  // namespace tgfx
