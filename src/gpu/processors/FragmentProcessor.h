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
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/SamplerState.h"
#include "gpu/SamplingArgs.h"
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

  template <typename T, size_t (FragmentProcessor::*COUNT)() const>
  class BuilderInputProvider {
   public:
    BuilderInputProvider(const FragmentProcessor* fp, const T* t) : fragmentProcessor(fp), t(t) {
    }

    const T& operator[](size_t i) const {
      return t[i];
    }

    size_t count() const {
      return (fragmentProcessor->*COUNT)();
    }

    BuilderInputProvider childInputs(size_t childIndex) const;

   private:
    const FragmentProcessor* fragmentProcessor;
    const T* t;
  };

  using TransformedCoordVars =
      BuilderInputProvider<ShaderVar, &FragmentProcessor::numCoordTransforms>;
  using TextureSamplers =
      BuilderInputProvider<SamplerHandle, &FragmentProcessor::numTextureSamplers>;

  struct EmitArgs {
    EmitArgs(FragmentShaderBuilder* fragBuilder, UniformHandler* uniformHandler,
             std::string outputColor, std::string inputColor, std::string inputSubset,
             const TransformedCoordVars* transformedCoords, const TextureSamplers* textureSamplers,
             std::function<std::string(std::string_view)> coordFunc = {})
        : fragBuilder(fragBuilder), uniformHandler(uniformHandler),
          outputColor(std::move(outputColor)), inputColor(std::move(inputColor)),
          transformedCoords(transformedCoords), textureSamplers(textureSamplers),
          coordFunc(std::move(coordFunc)), inputSubset(std::move(inputSubset)) {
    }

    /**
     * Interface used to emit code in the shaders.
     */
    FragmentShaderBuilder* fragBuilder;
    UniformHandler* uniformHandler;
    /**
     * A predefined vec4 in the FS in which the stage should place its output color (or coverage).
     */
    const std::string outputColor;
    /**
     * A vec4 that holds the input color to the stage in the FS.
     */
    const std::string inputColor;
    /**
     * Fragment shader variables containing the coords computed using each of the
     * FragmentProcessor's CoordTransforms.
     */
    const TransformedCoordVars* transformedCoords;
    /**
     * Contains one entry for each Texture of the Processor. These can be passed to the builder to
     * emit texture reads in the generated code.
     */
    const TextureSamplers* textureSamplers;
    const std::function<std::string(std::string_view)> coordFunc;
    const std::string inputSubset;
  };

  /**
   * Called when the program stage should insert its code into the shaders. The code in each shader
   * will be in its own block ({}) and so locally scoped names will not collide across stages.
   */
  virtual void emitCode(EmitArgs& args) const = 0;

  void setData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const;

  /**
   * Emit the child with the default input color (solid white)
   */
  void emitChild(size_t childIndex, std::string* outputColor, EmitArgs& parentArgs,
                 std::function<std::string(std::string_view)> coordFunc = {}) const {
    emitChild(childIndex, "", outputColor, parentArgs, std::move(coordFunc));
  }

  /**
   * Will emit the code of a child proc in its own scope. Pass in the parent's EmitArgs and
   * emitChild will automatically extract the coords and samplers of that child and pass them
   * on to the child's emitCode(). Also, any uniforms or functions emitted by the child will
   * have their names mangled to prevent redefinitions. The output color name is also mangled
   * therefore in an in/out param. It will be declared in mangled form by emitChild(). It is
   * legal to pass empty string as inputColor, since all fragment processors are required to work
   * without an input color.
   */
  void emitChild(size_t childIndex, const std::string& inputColor, std::string* outputColor,
                 EmitArgs& parentArgs,
                 std::function<std::string(std::string_view)> coordFunc = {}) const;

  /**
   * Variation that uses the parent's output color variable to hold the child's output.
   */
  void emitChild(size_t childIndex, const std::string& inputColor, EmitArgs& parentArgs) const;

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

  void internalEmitChild(size_t, const std::string&, const std::string&, EmitArgs&,
                         std::function<std::string(std::string_view)> = {}) const;

  std::vector<const CoordTransform*> coordTransforms;
};
}  // namespace tgfx
