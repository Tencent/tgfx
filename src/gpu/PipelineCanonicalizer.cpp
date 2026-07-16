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

#include "PipelineCanonicalizer.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

enum class GPCategory {
  DefaultGP,
  QuadPerEdgeAA,
  Other,
};

static GPCategory InferGPCategory(const DrawOp* op) {
  auto opType = op->type();
  switch (opType) {
    case DrawOp::Type::ShapeDrawOp:
      return GPCategory::DefaultGP;
    case DrawOp::Type::RectDrawOp:
      return GPCategory::QuadPerEdgeAA;
    default:
      return GPCategory::Other;
  }
}

static bool IsKnownColorFilter(const std::string& fpName) {
  return fpName == "ColorSpaceXformEffect" || fpName == "LumaFragmentProcessor" ||
         fpName == "AlphaStepFragmentProcessor" || fpName == "ColorMatrixFragmentProcessor";
}

static bool IsSimpleTextureEffect(const FragmentProcessor* fp) {
  if (fp == nullptr || fp->name() != "TextureEffect") {
    return false;
  }
  auto* te = static_cast<const TextureEffect*>(fp);
  return !te->isYUV() && te->numTextureSamplers() > 0;
}

static bool IsCanonicalGradient(const FragmentProcessor* fp) {
  if (fp->name() != "ClampedGradientEffect") {
    return false;
  }
  if (fp->numChildProcessors() != 2) {
    return false;
  }
  auto colorizer = fp->childProcessor(0);
  auto colorizerName = colorizer->name();
  if (colorizerName != "UnrolledBinaryGradientColorizer" &&
      colorizerName != "SingleIntervalGradientColorizer" &&
      colorizerName != "DualIntervalGradientColorizer" &&
      colorizerName != "TextureGradientColorizer") {
    return false;
  }
  auto layout = fp->childProcessor(1);
  auto layoutName = layout->name();
  if (layoutName != "LinearGradientLayout" && layoutName != "RadialGradientLayout" &&
      layoutName != "ConicGradientLayout" && layoutName != "DiamondGradientLayout") {
    return false;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return false;
  }
  return true;
}

static bool IsCanonicalCompose(const FragmentProcessor* fp) {
  if (fp->numChildProcessors() != 2) {
    return false;
  }
  auto child0 = fp->childProcessor(0);
  auto child1 = fp->childProcessor(1);
  if (child0 == nullptr || child0->name() != "TextureEffect") {
    return false;
  }
  auto* te = static_cast<const TextureEffect*>(child0);
  if (te->isYUV() || te->isAlphaOnly() || te->hasRGBAAA()) {
    return false;
  }
  if (child1 == nullptr) {
    return false;
  }
  return IsKnownColorFilter(child1->name());
}

static bool IsCanonicalXfermode(const FragmentProcessor* fp) {
  auto* xfp = static_cast<const XfermodeFragmentProcessor*>(fp);
  for (size_t i = 0; i < xfp->numChildProcessors(); i++) {
    auto child = xfp->childProcessor(i);
    if (child == nullptr) {
      continue;
    }
    auto childName = child->name();
    if (childName == "TextureEffect") {
      auto* te = static_cast<const TextureEffect*>(child);
      if (te->isYUV() || te->numTextureSamplers() == 0) {
        return false;
      }
    } else if (childName == "ConstColorProcessor") {
      if (i != 0) {
        return false;
      }
    } else if (childName == "TiledTextureEffect") {
      if (i != 0) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

static bool IsCanonicalBlur(const FragmentProcessor* fp) {
  if (fp->numChildProcessors() != 1) {
    return false;
  }
  auto child = fp->childProcessor(0);
  if (child == nullptr || child->name() != "TextureEffect") {
    return false;
  }
  auto* te = static_cast<const TextureEffect*>(child);
  return te->numTextureSamplers() > 0;
}

static bool IsCanonical(const FragmentProcessor* fp, GPCategory gpCat) {
  if (fp == nullptr) {
    return true;
  }
  auto fpName = fp->name();

  if (fpName == "TextureEffect") {
    auto* te = static_cast<const TextureEffect*>(fp);
    return !te->isYUV() && te->numTextureSamplers() > 0;
  }

  if (fpName == "TiledTextureEffect") {
    if (gpCat != GPCategory::DefaultGP) {
      return false;
    }
    auto* tte = static_cast<const TiledTextureEffect*>(fp);
    return !tte->hasPerspective();
  }

  if (fpName == "ConstColorProcessor") {
    return gpCat == GPCategory::DefaultGP;
  }

  if (fpName == "DeviceSpaceTextureEffect") {
    return gpCat == GPCategory::DefaultGP;
  }

  if (fpName == "ClampedGradientEffect") {
    return IsCanonicalGradient(fp);
  }

  if (fpName == "ComposeFragmentProcessor") {
    return IsCanonicalCompose(fp);
  }

  if (fpName == "XfermodeFragmentProcessor - dst" || fpName == "XfermodeFragmentProcessor - src" ||
      fpName == "XfermodeFragmentProcessor - two") {
    return IsCanonicalXfermode(fp);
  }

  if (fpName == "GaussianBlur1DFragmentProcessor") {
    return IsCanonicalBlur(fp);
  }

  if (fpName == "ColorSpaceXformEffect" || fpName == "LumaFragmentProcessor" ||
      fpName == "AlphaStepFragmentProcessor" || fpName == "ColorMatrixFragmentProcessor") {
    return true;
  }

  return false;
}

static PlacementPtr<FragmentProcessor> FlattenFullscreen(Context* context,
                                                         PlacementPtr<FragmentProcessor> fp,
                                                         int width, int height) {
  if (fp == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto intermediateRT = RenderTargetProxy::Make(context, width, height, false, 1, false,
                                                ImageOrigin::TopLeft, BackingFit::Approx);
  if (intermediateRT == nullptr) {
    return nullptr;
  }
  auto drawingManager = context->drawingManager();
  if (!drawingManager->fillRTWithFP(intermediateRT, std::move(fp), 0)) {
    return nullptr;
  }
  auto textureProxy = intermediateRT->asTextureProxy();
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  return TextureEffect::Make(allocator, std::move(textureProxy));
}

static void ReduceMultipleColorFPs(Context* context,
                                   std::vector<PlacementPtr<FragmentProcessor>>& colors, int width,
                                   int height) {
  auto allocator = context->drawingAllocator();

  // Strategy: try to reduce the FP chain to a single canonical FP that will hit a precompiled
  // shader. If no reduction leads to a precompiled hit, re-compose into a single
  // ComposeFragmentProcessor and let dynamic compilation handle it (preserving quality).

  if (colors.size() == 2 && colors[0] && colors[1]) {
    auto secondName = colors[1]->name();
    // Case A: first is a simple TextureEffect and second is a known color filter.
    // This directly matches TryMatchComposedTexture.
    if (IsSimpleTextureEffect(colors[0].get()) && IsKnownColorFilter(secondName)) {
      auto* te = static_cast<const TextureEffect*>(colors[0].get());
      if (!te->isAlphaOnly() && !te->hasRGBAAA()) {
        auto composed =
            ComposeFragmentProcessor::Make(allocator, std::move(colors[0]), std::move(colors[1]));
        colors.clear();
        if (composed) {
          colors.emplace_back(std::move(composed));
        }
        return;
      }
    }
    // Case B: first FP is not a simple texture but can be flattened to one, and second is a
    // known color filter. Flatten the first to a TextureEffect, then compose to match
    // TryMatchComposedTexture.
    if (IsKnownColorFilter(secondName) && !IsSimpleTextureEffect(colors[0].get())) {
      auto flatFirst = FlattenFullscreen(context, std::move(colors[0]), width, height);
      if (flatFirst) {
        auto composed =
            ComposeFragmentProcessor::Make(allocator, std::move(flatFirst), std::move(colors[1]));
        colors.clear();
        if (composed) {
          colors.emplace_back(std::move(composed));
        }
        return;
      }
    }
  }

  // For chains of 3+ FPs, flatten all but the last into intermediate textures.
  // If the last FP is a known color filter, compose for a precompiled hit.
  // Otherwise flatten everything to a single TextureEffect.
  if (colors.size() >= 3) {
    std::shared_ptr<TextureProxy> intermediateTexture = nullptr;
    for (size_t i = 0; i < colors.size() - 1; ++i) {
      auto intermediateRT = RenderTargetProxy::Make(context, width, height, false, 1, false,
                                                    ImageOrigin::TopLeft, BackingFit::Approx);
      if (!intermediateRT) {
        break;
      }
      PlacementPtr<FragmentProcessor> passProcessor;
      if (i == 0) {
        passProcessor = std::move(colors[i]);
      } else {
        auto texFP = TextureEffect::Make(allocator, intermediateTexture);
        if (!texFP) {
          break;
        }
        passProcessor =
            ComposeFragmentProcessor::Make(allocator, std::move(texFP), std::move(colors[i]));
      }
      if (!passProcessor) {
        break;
      }
      auto drawingManager = context->drawingManager();
      if (!drawingManager->fillRTWithFP(intermediateRT, std::move(passProcessor), 0)) {
        break;
      }
      intermediateTexture = intermediateRT->asTextureProxy();
      if (!intermediateTexture) {
        break;
      }
    }

    if (intermediateTexture) {
      auto texFP = TextureEffect::Make(allocator, intermediateTexture);
      auto& lastFP = colors.back();
      if (lastFP && IsKnownColorFilter(lastFP->name()) && texFP) {
        auto composed =
            ComposeFragmentProcessor::Make(allocator, std::move(texFP), std::move(lastFP));
        colors.clear();
        if (composed) {
          colors.emplace_back(std::move(composed));
        }
        return;
      }
      if (texFP && lastFP) {
        auto composed =
            ComposeFragmentProcessor::Make(allocator, std::move(texFP), std::move(lastFP));
        colors.clear();
        auto flat = FlattenFullscreen(context, std::move(composed), width, height);
        if (flat) {
          colors.emplace_back(std::move(flat));
        }
        return;
      }
      if (texFP) {
        colors.clear();
        colors.emplace_back(std::move(texFP));
        return;
      }
    }

    // Fallback: compose all and flatten.
    auto composed = ComposeFragmentProcessor::Make(allocator, std::move(colors));
    colors.clear();
    auto flat = FlattenFullscreen(context, std::move(composed), width, height);
    if (flat) {
      colors.emplace_back(std::move(flat));
    }
    return;
  }

  // For 2 FPs where no precompiled pattern applies: leave them as separate color FPs.
  // The pipeline handles multiple color FPs via dynamic compilation (ProgramBuilder chains
  // them sequentially). This preserves the exact behavior of the original code path.
}

void PipelineCanonicalizer::Canonicalize(Context* context, DrawOp* op,
                                         const std::shared_ptr<RenderTargetProxy>& renderTarget) {
  if (op->hasXferProcessor()) {
    return;
  }
  auto gpCat = InferGPCategory(op);
  if (gpCat == GPCategory::Other) {
    return;
  }

  auto& colors = op->colorProcessors();
  if (colors.empty()) {
    return;
  }

  int rtWidth = renderTarget->width();
  int rtHeight = renderTarget->height();

  // Step 1: Unwrap a single ComposeFragmentProcessor into its children for processing.
  if (colors.size() == 1 && colors[0] && colors[0]->name() == "ComposeFragmentProcessor") {
    auto compose = static_cast<ComposeFragmentProcessor*>(colors[0].get());
    auto children = compose->takeChildren();
    colors.clear();
    colors = std::move(children);
  }

  // Step 2: Reduce multiple color FPs to a single canonical FP.
  if (colors.size() >= 2) {
    ReduceMultipleColorFPs(context, colors, rtWidth, rtHeight);
  }

  // Step 3: Ensure the single remaining FP is in canonical form for precompiled shaders.
  // ComposeFragmentProcessor and XfermodeFragmentProcessor with non-canonical children are left
  // for dynamic compilation — flattening them would introduce precision loss with no precompiled
  // hit benefit. Only flatten FPs that are definitively incompatible with the current GP (e.g.,
  // TiledTextureEffect on QuadPerEdgeAAGP).
  if (colors.size() == 1 && colors[0] && !IsCanonical(colors[0].get(), gpCat)) {
    auto fpName = colors[0]->name();
    bool shouldFlatten = false;
    if (fpName == "TiledTextureEffect" && gpCat != GPCategory::DefaultGP) {
      shouldFlatten = true;
    } else if (fpName == "ConstColorProcessor" && gpCat != GPCategory::DefaultGP) {
      shouldFlatten = true;
    } else if (fpName == "DeviceSpaceTextureEffect" && gpCat != GPCategory::DefaultGP) {
      shouldFlatten = true;
    }
    if (shouldFlatten) {
      auto replacement = FlattenFullscreen(context, std::move(colors[0]), rtWidth, rtHeight);
      colors.clear();
      if (replacement) {
        colors.emplace_back(std::move(replacement));
      }
    }
  }
}

}  // namespace tgfx
