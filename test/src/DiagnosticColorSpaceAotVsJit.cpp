// Diagnostic, not a regression test: reproduces the DrawSRGBDropShadowFilterToP3 scene of
// CanvasTest.ColorSpace (a known pixel-baseline failure) and prints, per paint, which kernels the
// precompiled route hit and the AOT vs JIT pixel difference, prefixed "[CSProbe]". It asserts only
// that the scene renders; use it to tell whether a ColorSpace diff comes from the precompiled
// route or from the baseline itself:
//   TGFXFullTest_OpenGL --gtest_filter='DiagnosticColorSpaceAotVsJit.*'
#include <cstdio>
#include <cstdlib>
#include "base/TGFXTest.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/TestUtils.h"

namespace tgfx {

static bool gGradient = true;
static bool gBlendFilter = true;

static Bitmap RenderScene(Context* context, bool withMask, bool withShadow, bool p3) {
  auto cs = p3 ? ColorSpace::DisplayP3() : ColorSpace::SRGB();
  auto surface = Surface::Make(context, 1024, 1024, false, 1, false, 0, cs);
  auto canvas = surface->getCanvas();
  Paint paint;
  if (gGradient) {
    paint.setShader(
        Shader::MakeRadialGradient(Point::Make(512, 512), 500, {Color::Green(), Color::Red()}, {}));
  } else {
    paint.setShader(Shader::MakeColorShader(Color::Green()));
  }
  if (gBlendFilter) {
    paint.setColorFilter(ColorFilter::Blend(Color::FromRGBA(0, 0, 125, 125), BlendMode::SrcOver));
  }
  if (withMask) {
    auto image1 = MakeImage("resources/apitest/imageReplacement.png");
    auto maskShader = Shader::MakeImageShader(std::move(image1), TileMode::Decal, TileMode::Decal);
    auto maskFilter = MaskFilter::MakeShader(std::move(maskShader));
    maskFilter = maskFilter->makeWithMatrix(Matrix::MakeTrans(462, 462));
    paint.setMaskFilter(maskFilter);
  }
  if (withShadow) {
    paint.setImageFilter(ImageFilter::DropShadow(-10, -10, 10, 10, Color::Green()));
  }
  canvas->drawPaint(paint);
  context->flushAndSubmit(true);
  Bitmap bitmap;
  bitmap.allocPixels(1024, 1024);
  auto* pixels = bitmap.lockPixels();
  surface->readPixels(bitmap.info(), pixels);
  bitmap.unlockPixels();
  return bitmap;
}

static void Diff(const char* label, Bitmap& a, Bitmap& b) {
  auto* pa = static_cast<const uint8_t*>(a.lockPixels());
  auto* pb = static_cast<const uint8_t*>(b.lockPixels());
  size_t diff = 0;
  int maxDelta[4] = {0, 0, 0, 0};
  size_t perChannel[4] = {0, 0, 0, 0};
  int firstX = -1;
  int firstY = -1;
  uint8_t fa[4] = {};
  uint8_t fb[4] = {};
  for (int y = 0; y < 1024; ++y) {
    for (int x = 0; x < 1024; ++x) {
      size_t o = (static_cast<size_t>(y) * 1024 + static_cast<size_t>(x)) * 4;
      bool any = false;
      for (int c = 0; c < 4; ++c) {
        int d = std::abs(static_cast<int>(pa[o + static_cast<size_t>(c)]) -
                         static_cast<int>(pb[o + static_cast<size_t>(c)]));
        if (d > 0) {
          any = true;
          perChannel[c]++;
          if (d > maxDelta[c]) {
            maxDelta[c] = d;
          }
        }
      }
      if (any) {
        if (firstX < 0) {
          firstX = x;
          firstY = y;
          for (int c = 0; c < 4; ++c) {
            fa[c] = pa[o + static_cast<size_t>(c)];
            fb[c] = pb[o + static_cast<size_t>(c)];
          }
        }
        diff++;
      }
    }
  }
  std::printf(
      "[CSProbe] %-34s diffPixels=%zu maxDelta(rgba)=%d,%d,%d,%d perChannel=%zu,%zu,%zu,%zu\n",
      label, diff, maxDelta[0], maxDelta[1], maxDelta[2], maxDelta[3], perChannel[0], perChannel[1],
      perChannel[2], perChannel[3]);
  if (firstX >= 0) {
    std::printf("[CSProbe]   first diff at (%d,%d): a=%02x%02x%02x%02x b=%02x%02x%02x%02x\n",
                firstX, firstY, fa[0], fa[1], fa[2], fa[3], fb[0], fb[1], fb[2], fb[3]);
  }
  a.unlockPixels();
  b.unlockPixels();
}

TGFX_TEST(DiagnosticColorSpaceAotVsJit, Run) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  struct Case {
    const char* label;
    bool mask;
    bool shadow;
    bool p3;
  };
  const Case cases[] = {
      {"srgb: shadow+mask", true, true, false}, {"p3:   shadow+mask", true, true, true},
      {"p3:   shadow only", false, true, true}, {"p3:   mask only", true, false, true},
      {"p3:   plain", false, false, true},
  };
  struct Paints {
    const char* name;
    bool gradient;
    bool blend;
  };
  const Paints paints[] = {{"gradient+blendFilter", true, true},
                           {"gradient only", true, false},
                           {"color+blendFilter", false, true}};
  for (const auto& p : paints) {
    gGradient = p.gradient;
    gBlendFilter = p.blend;
    std::printf("[CSProbe] ===== paint: %s =====\n", p.name);
    for (const auto& c : cases) {
      cache->unload();
      context->globalCache()->clearPrograms();
      auto jit = RenderScene(context, c.mask, c.shadow, c.p3);
      auto bundle = EmbeddedShaderBundles::GetBundle(context->backend());
      ASSERT_TRUE(cache->loadBundle(bundle.first, bundle.second));
      context->globalCache()->clearPrograms();
      cache->setDiagnosticRecordingEnabled(true);
      cache->resetStats();
      auto aot = RenderScene(context, c.mask, c.shadow, c.p3);
      std::printf("[CSProbe] %s hits:", c.label);
      for (const auto& hit : cache->hitRecords()) {
        std::printf(" %s(v%u,f%u)", hit.shaderName.c_str(),
                    static_cast<unsigned>(hit.vertPermutationIndex),
                    static_cast<unsigned>(hit.fragPermutationIndex));
      }
      std::printf("\n");
      {
        auto st = cache->drawStats();
        std::printf(
            "[CSProbe]   stats: draws=%u directChain=%u offscreenPlan=%u fpFlattenEdges=%u "
            "planMaterializedEdges=%u\n",
            static_cast<unsigned>(st.draws), static_cast<unsigned>(st.directChainDraws),
            static_cast<unsigned>(st.offscreenPlanDraws), static_cast<unsigned>(st.fpFlattenEdges),
            static_cast<unsigned>(st.planMaterializedEdges));
      }
      cache->setDiagnosticRecordingEnabled(false);
      Diff(c.label, aot, jit);
    }
  }
  cache->unload();
}

}  // namespace tgfx
