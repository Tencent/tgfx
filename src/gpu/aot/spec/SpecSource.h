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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "gpu/Uniform.h"
#include "gpu/UniformData.h"
#include "gpu/aot/spec/SpecValues.h"

namespace tgfx {

/**
 * What the specialization layer needs from the artifact store. SpecResolver reads
 * the bundle's reflected layouts and record their outcome through this interface, so the spec layer
 * never includes the bundle implementation that provides them — the cache implements this and is the
 * only thing that knows how a bundle is stored.
 */
class SpecSource {
 public:
  virtual ~SpecSource() = default;

  /// Whether the enable switch (build, backend or environment) admits specialization at all.
  virtual bool specializationEnabled() const = 0;
  /// Whether a bundle is loaded; without one there are no reflected layouts to capture against.
  virtual bool isLoaded() const = 0;
  /// The bundle's profile tag, part of every stage key.
  virtual const std::string& profileTag() const = 0;
  /// The bundle's payload format, which decides whether the backend can consume the values.
  virtual uint16_t payloadFormat() const = 0;

  /// The fragment stage's spec table, or null when the stage carries no freezable fields.
  virtual const std::vector<SpecFieldInfo>* findFragmentSpecFields(uint32_t variantId) const = 0;

  /// Records one derivation for the cost statistics (empty values count as a generic-form serve).
  virtual void recordSpecCapture(const SpecValues& values) = 0;
};

}  // namespace tgfx
