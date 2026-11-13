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

#include <functional>
#include "gpu/CoordTransform.h"
#include "gpu/SamplerState.h"
#include "gpu/SamplingArgs.h"
#include "gpu/UniformData.h"
#include "gpu/processors/Processor.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class AOTNodeBuilder;
class AOTNodeID;
struct AOTSamplerRef;
class ParamWriter;
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

  virtual bool lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input, AOTNodeID* output) const;

  /**
   * Appends the samplers this processor itself declares (not its children's) in the order the
   * legacy texture-sampler enumeration reported them. Lowering walks the tree once and records
   * the result on the draw IR, so the runtime never enumerates processors to bind samplers.
   */
  virtual void appendSamplers(std::vector<AOTSamplerRef>* /*samplers*/) const {
  }

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
    // Empty iteration: the starting point of a CoordTransformIter that walks a caller-supplied
    // transform sequence instead of a processor tree.
    Iter() = default;

    explicit Iter(const FragmentProcessor* fp) {
      fpStack.push_back(fp);
    }

    const FragmentProcessor* next();

   private:
    std::vector<const FragmentProcessor*> fpStack;
  };

  /**
   * Iterates a caller-supplied sequence of coordinate transforms: the AOT upload feeds the
   * geometry writer the leaf-ordered transforms the compiled program's binding table describes,
   * so no processor tree exists to walk. The sequence must outlive the iterator.
   */
  class CoordTransformIter {
   public:
    explicit CoordTransformIter(const std::vector<CoordTransform>* transforms);

    const CoordTransform* next();

   private:
    const std::vector<CoordTransform>* transforms = nullptr;
    size_t transformIndex = 0;
  };

 protected:
  std::vector<PlacementPtr<FragmentProcessor>> childProcessors = {};

  explicit FragmentProcessor(uint32_t classID) : Processor(classID) {
  }

  /**
   * FragmentProcessor subclasses call this from their constructor to register any child
   * FragmentProcessors they have. This must be called AFTER all texture accesses and coord
   * transforms have been added.
   * This is for processors whose shader code will be composed of nested processors whose output
   * colors will be combined somehow to produce its output color. Registering these child
   * processors lets the program writers walk them for transformed coords and texture accesses.
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

 private:
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
};
}  // namespace tgfx
