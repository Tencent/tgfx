//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>

namespace tgfx {

/// Reads a whole file as text. Returns an empty string when the file cannot be opened.
std::string ReadFileContents(const std::string& path);

/// Expands #include "file" lines, resolving every path against baseDir. This is the exact text the
/// compiler receives (before the variant #defines are prepended), so the source digest hashes the
/// result of this function rather than the raw files.
std::string ResolveIncludes(const std::string& source, const std::string& baseDir);

}  // namespace tgfx
