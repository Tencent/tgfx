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

#include <cstddef>

namespace tgfx {
class FragmentProcessor;

/**
 * Identifies how a child FragmentProcessor is consumed by its parent. The consumer's effect domain
 * determines whether a materialization edge is beneficial and, for neighborhood consumers, how much
 * apron the materialized texture must carry so the consumer's samples remain correct.
 */
enum class MaterializationConsumer {
  /// A pointwise blend (XfermodeFragmentProcessor child). The child is sampled at the same
  /// coordinate as the output, so no apron is required.
  PointwiseBlend,
};

/**
 * The decision produced by the materialization policy for a single child in a single consumer
 * context.
 */
struct MaterializationDecision {
  /// True when the child should be rendered into an offscreen texture and replaced by a plain
  /// TextureEffect so both the producer and the downstream consumer become AOT-matchable kernels.
  bool shouldFlatten = false;
  /// The consumer's sampling radius in pixels. The materialized texture must be expanded by this
  /// much on every side (an apron) so a neighborhood consumer never samples past the valid region.
  /// Zero for pointwise consumers.
  float apronRadius = 0.0f;
};

/**
 * AOTMaterializationPolicy is the single authority that decides where to insert materialization
 * edges to convert an otherwise unmatchable FragmentProcessor chain into a sequence of
 * AOT-matchable kernels. Callers at FragmentProcessor-build sites consult it instead of hand-coding
 * their own "is this child simple enough" predicates, so the decomposition behavior stays
 * consistent across BlendShader, ColorFilterShader, image filters, and future sites.
 */
class AOTMaterializationPolicy {
 public:
  /**
   * Decides whether the given child must be materialized to remain AOT-matchable when consumed in
   * the given context. This reflects AOT matchability only; it does not consider correctness
   * constraints that force flattening regardless (those are handled separately by the caller).
   *
   * @param child       The child FragmentProcessor, or nullptr for an absent child.
   * @param consumer    How the child is consumed by its parent.
   * @param childIndex  The child's position in its parent (0 = src, 1 = dst for a blend).
   * @return            The materialization decision for this child.
   */
  static MaterializationDecision Evaluate(const FragmentProcessor* child,
                                          MaterializationConsumer consumer, size_t childIndex);
};

}  // namespace tgfx
