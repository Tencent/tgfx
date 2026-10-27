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

namespace tgfx {

/// Writes contents to path unless the file already holds exactly these bytes, in which case it is
/// left untouched, timestamp included. The build relies on that: the generation step is marked
/// restat, so an unchanged bundle stops the rebuild before embedding, compiling and linking.
///
/// A changed file is written to a temporary file in the same directory and renamed over path, so
/// readers never see a partial file and a failed write leaves the previous file intact. Returns
/// false with *error set on failure; *changed (when given) reports whether path was replaced.
bool WriteFileIfChanged(const std::string& path, const std::string& contents, bool* changed,
                        std::string* error);

}  // namespace tgfx
