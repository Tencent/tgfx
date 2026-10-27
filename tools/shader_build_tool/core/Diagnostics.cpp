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
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "Diagnostics.h"

namespace tgfx {

void Diagnostics::registerProfile(const std::string& profileTag) {
  profileErrorCounts_.emplace(profileTag, 0);
}

void Diagnostics::recordCommonError() {
  for (auto& profile : profileErrorCounts_) {
    profile.second++;
  }
}

void Diagnostics::recordProfileError(const std::string& profileTag) {
  profileErrorCounts_[profileTag]++;
}

const std::map<std::string, uint64_t>& Diagnostics::errorCounts() const {
  return profileErrorCounts_;
}

}  // namespace tgfx
