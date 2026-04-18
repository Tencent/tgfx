/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "gpu/CoordTransform.h"
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/MangledResources.h"
#include "gpu/SamplerState.h"
#include "gpu/SamplingArgs.h"
#include "gpu/ShaderCallManifest.h"
#include "gpu/ShaderMacroSet.h"
#include "gpu/UniformData.h"
#include "gpu/UniformHandler.h"
#include "gpu/processors/Processor.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class ProgramInfo;
class Image;
class Shader;

class FPArgs {
 public:
  FPArgs() = default;

  FPArgs(Context* context, uint32_t renderFlags, const Rect& drawRect, float drawScale = 1.0f)
      : context(context), renderFlags(renderFlags), drawRect(drawRect), drawScale(drawScale) {
  }

  Context* context = nullptr;
  uint32_t renderFlags = 0;
  Rect drawRect = {};
  float drawScale = 1.0f;
};

class FragmentProcessor : public Processor {
 public:
  /**
   * Creates a fragment processor that will draw the given image with the given options. The both
   * tileModeX and tileModeY are set to TileMode::Clamp.
   */
  static PlacementPtr<FragmentProcessor> Make(
      std::shared_ptr<Image> image, const FPArgs& args, const SamplingOptions& sampling,
      SrcRectConstraint constraint = SrcRectConstraint::Fast, const Matrix* uvMatrix = nullptr);

  /**
   * Creates a fragment processor that will draw the given image with the given options.
   */
  static PlacementPtr<FragmentProcessor> Make(std::shared_ptr<Image> image, const FPArgs& args,
                                              TileMode tileModeX, TileMode tileModeY,
                                              const SamplingOptions& sampling,
                                              SrcRectConstraint constraint,
                                              const Matrix* uvMatrix = nullptr);
  /**
   * Creates a fragment processor that will draw the given image with the given options.
   * The samplingArgs contains additional information about how to sample the image.
   */
  static PlacementPtr<FragmentProcessor> Make(std::shared_ptr<Image> image, const FPArgs& args,
                                              const SamplingArgs& samplingArgs,
                                              const Matrix* uvMatrix = nullptr);

  /**
   * Creates a fragment processor that will draw the given Shader with the given options.
   */
  static PlacementPtr<FragmentProcessor> Make(
      std::shared_ptr<Shader> shader, const FPArgs& args, const Matrix* uvMatrix = nullptr,
      const std::shared_ptr<ColorSpace>& dstColorSpace = nullptr);

  /**
   *  In many instances (e.g., Shader::asFragmentProcessor() implementations) it is desirable to
   *  only consider the input color's alpha. However, there is a competing desire to have reusable
   *  FragmentProcessor subclasses that can be used in other scenarios where the entire input
   *  color is considered or ignored. This function exists to filter the input color and pass it to
   *  an FP. It does so by returning a parent FP that multiplies the passed in FPs output by the
   *  parent's input alpha. The passed in FP will not receive an input color.
   */
  static PlacementPtr<FragmentProcessor> MulChildByInputAlpha(
      BlockAllocator* allocator, PlacementPtr<FragmentProcessor> child);

  /**
   * Returns the input color, modulated by the child's alpha. The passed in FP will not receive an
   * input color.
   * @param inverted false: output = input * child.a; true: output = input * (1 - child.a)
   */
  static PlacementPtr<FragmentProcessor> MulInputByChildAlpha(BlockAllocator* allocator,
                                                              PlacementPtr<FragmentProcessor> child,
                                                              bool inverted = false);

  /**
   * Returns a fragment processor that composes two fragment processors into second(first(x)).
   * This is equivalent to running them in series (first, then second). This is not the same as
   * transfer-mode composition; there is no blending step.
   */
  static PlacementPtr<FragmentProcessor> Compose(BlockAllocator* allocator,
                                                 PlacementPtr<FragmentProcessor> first,
                                                 PlacementPtr<FragmentProcessor> second);

  size_t numTextureSamplers() const {
    return onCountTextureSamplers();
  }

  std::shared_ptr<Texture> textureAt(size_t i) const {
    return onTextureAt(i);
  }

  SamplerState samplerStateAt(size_t i) const {
    return onSamplerStateAt(i);
  }

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

  size_t numChildProcessors() const {
    return childProcessors.size();
  }

  const FragmentProcessor* childProcessor(size_t index) const {
    return childProcessors[index].get();
  }

  size_t numCoordTransforms() const {
    return coordTransforms.size();
  }

  /**
   * Returns the coordinate transformation at index. index must be valid according to
   * numCoordTransforms().
   */
  const CoordTransform* coordTransform(size_t index) const {
    return coordTransforms[index];
  }

  /**
   * Pre-order traversal of a FP hierarchy, or of the forest of FPs in a ProgramInfo. In the latter
   * case the tree rooted at each FP in the ProgramInfo is visited successively.
   */
  class Iter {
   public:
    explicit Iter(const FragmentProcessor* fp) {
      fpStack.push_back(fp);
    }

    explicit Iter(const ProgramInfo* programInfo);

    const FragmentProcessor* next();

   private:
    std::vector<const FragmentProcessor*> fpStack;
  };

  /**
   * Iterates over all the CoordTransforms owned by the forest of FragmentProcessors in a
   * ProgramInfo.
   */
  class CoordTransformIter {
   public:
    explicit CoordTransformIter(const ProgramInfo* programInfo);

    const CoordTransform* next();

   private:
    const FragmentProcessor* currFP = nullptr;
    size_t currentIndex = 0;
    FragmentProcessor::Iter fpIter;
  };

  void setData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const;

 protected:
  std::vector<PlacementPtr<FragmentProcessor>> childProcessors = {};

  explicit FragmentProcessor(uint32_t classID) : Processor(classID) {
  }

  /**
   * FragmentProcessor subclasses call this from their constructor to register any child
   * FragmentProcessors they have. This must be called AFTER all texture accesses and coord
   * transforms have been added.
   * This is for processors whose shader code will be composed of nested processors whose output
   * colors will be combined somehow to produce its output color.  Registering these child
   * processors will allow the ProgramBuilder to automatically handle their transformed coords and
   * texture accesses and mangle their uniform and output color names.
   */
  size_t registerChildProcessor(PlacementPtr<FragmentProcessor> child);

  /**
   * Fragment Processor subclasses call this from their constructor to register coordinate
   * transformations. Coord transforms provide a mechanism for a processor to receive coordinates
   * in their FS code. The matrix expresses a transformation from local space. For a given
   * fragment the matrix will be applied to the local coordinate that maps to the fragment.
   *
   * This must only be called from the constructor because Processors are immutable. The
   * processor subclass manages the lifetime of the transformations (this function only stores a
   * pointer). The CoordTransform is typically a member field of the Processor subclass.
   *
   * A processor subclass that has multiple methods of construction should always add its coord
   * transforms in a consistent order.
   */
  void addCoordTransform(const CoordTransform* transform) {
    coordTransforms.push_back(transform);
  }

  virtual void onSetData(UniformData*, UniformData*) const {
  }

  // ---- Modular shader virtual methods (for ModularProgramBuilder) ----

  /**
   * Subclasses override to declare their shader variant macros.
   * Called by ModularProgramBuilder to collect all macros for the #define preamble.
   */
  virtual void onBuildShaderMacros(ShaderMacroSet& /*macros*/) const {
  }

  /**
   * Returns the path to this Processor's .glsl function file (without extension prefix).
   * Example: "fragment/texture_effect.frag" for src/gpu/shaders/fragment/texture_effect.frag.glsl
   * Returns empty string if this Processor has no standalone .glsl file (e.g., ComposeFragmentProcessor).
   */
  virtual std::string shaderFunctionFile() const {
    return "";
  }

  /**
   * Computes the coord transform offset for the i-th child of this processor.
   */
  size_t computeChildCoordOffset(size_t parentCoordVarsIdx, size_t childIdx) const {
    size_t offset = parentCoordVarsIdx + numCoordTransforms();
    for (size_t i = 0; i < childIdx; ++i) {
      Iter iter(childProcessor(i));
      while (const auto* fp = iter.next()) {
        offset += fp->numCoordTransforms();
      }
    }
    return offset;
  }

  /**
   * Registers this Processor's uniforms/samplers with the UniformHandler and populates
   * the MangledResources with the resulting mangled names. Called before buildCallStatement().
   * Default is empty — subclasses that need uniforms/samplers must override.
   */
  virtual void declareResources(UniformHandler* /*uniformHandler*/, MangledUniforms& /*uniforms*/,
                                MangledSamplers& /*samplers*/) const {
  }

  /**
   * Generates the GLSL call statement for this Processor in the main() function body.
   * @param inputColorVar The input color variable name from the previous Processor.
   * @param fpIndex The index of this FP in the pipeline (for output variable naming).
   * @return ShaderCallManifest with the complete statement and output variable name.
   * Default returns empty — subclasses that have .glsl files must override.
   */
  virtual ShaderCallManifest buildCallStatement(const std::string& /*inputColorVar*/,
                                                int /*fpIndex*/,
                                                const MangledUniforms& /*uniforms*/,
                                                const MangledVaryings& /*varyings*/,
                                                const MangledSamplers& /*samplers*/) const {
    return {};
  }

  /**
   * Generates a GLSL call statement for a container FP that receives pre-computed child outputs.
   * @param inputColor The input color variable name.
   * @param childOutputs Output variable names from recursively-emitted child FPs (indexed 0..N-1).
   * @param uniforms Mangled uniform names registered by declareResources().
   * @param samplers Mangled sampler names collected from the FP subtree.
   * @param varyings Mangled varying names (coord transforms, subset var).
   */
  virtual ShaderCallManifest buildContainerCallStatement(
      const std::string& /*inputColor*/, const std::vector<std::string>& /*childOutputs*/,
      const MangledUniforms& /*uniforms*/, const MangledSamplers& /*samplers*/,
      const MangledVaryings& /*varyings*/) const {
    return {};
  }

  struct ChildEmitInfo {
    size_t childIndex;
    std::string inputOverride;  // empty = use parent input
    int useOutputOfChild = -1;  // >= 0: use output of the specified child as input
  };

  /**
   * Returns the emit plan for child FPs. Containers override this to provide custom input colors
   * for children (e.g., XfermodeFragmentProcessor passes opaque input to TwoChild children).
   */
  virtual std::vector<ChildEmitInfo> getChildEmitPlan(const std::string& /*parentInput*/) const {
    std::vector<ChildEmitInfo> plan;
    for (size_t i = 0; i < numChildProcessors(); ++i) {
      plan.push_back({i, ""});
    }
    return plan;
  }

 private:
  virtual void onComputeProcessorKey(BytesKey*) const {
  }

  virtual size_t onCountTextureSamplers() const {
    return 0;
  }

  virtual std::shared_ptr<Texture> onTextureAt(size_t) const {
    return nullptr;
  }

  virtual SamplerState onSamplerStateAt(size_t) const {
    return {};
  }

  std::vector<const CoordTransform*> coordTransforms;

  friend class ModularProgramBuilder;
};
}  // namespace tgfx
