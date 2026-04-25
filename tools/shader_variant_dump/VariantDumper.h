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

#include <ostream>
#include <string>
#include <vector>
#include "gpu/variants/ShaderVariant.h"

namespace tgfx {

/**
 * Metadata row for a single processor handled by the variant dumper. The dumper never
 * instantiates processors — it only invokes their static EnumerateVariants() entry points and
 * looks up their GLSL module source by name.
 */
struct VariantProcessorEntry {
  /** Canonical processor class name (matches ShaderModuleRegistry::HasModule input). */
  std::string name;

  /** "GeometryProcessor", "FragmentProcessor", or "XferProcessor". */
  std::string category;

  /** Returns the processor's variant list. Implemented as &ProcessorClass::EnumerateVariants. */
  std::vector<ShaderVariant> (*enumerate)() = nullptr;

  /** Override for ShaderModuleRegistry lookup when the processor's runtime name differs from its
   *  registry key (e.g. XfermodeFragmentProcessor's name() is suffixed with "- two/- dst/- src"
   *  at runtime). Empty means "use `name` above verbatim". */
  std::string moduleLookupKey;
};

/** Builds and returns the static list of processors the dumper knows about (33 entries). */
const std::vector<VariantProcessorEntry>& AllVariantProcessors();

/** Writes the full variant inventory as JSON into `out`. Never touches any GPU resources. */
void DumpAllVariants(std::ostream& out);

}  // namespace tgfx