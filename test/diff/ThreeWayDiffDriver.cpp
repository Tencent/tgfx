// Three-way diff driver (audit F02 / tasks 5-6). Renders a fixed scene matrix through the
// public rendering API only, so the exact same source builds against the merge-base baseline
// (no AOT code at all), the branch with TGFX_AOT_DISABLE set (pure runtime route, no
// materialization), and the branch with AOT fully enabled. Each scene is written as one raw
// RGBA_8888 file plus a manifest; test/diff/compare_scenes.py computes the per-scene
// differences. Bump kSceneRevision whenever a scene is added or changed so stale output
// directories are rejected instead of compared.
#include <array>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/opengl/GLDevice.h"

namespace {

// Bump whenever a scene is added or changed; compare_scenes.py rejects mismatched manifests.
// Revision 3: legal float paint alpha (Paint::setAlpha takes 0-1, the previous revision passed
// raw 8-bit values), premultiplied alpha-ramp inputs, distinct gradients on both blend sides,
// visible inner branches in nested scenes, and a genuinely non-pixel-aligned AA clip.
constexpr int kSceneRevision = 3;
constexpr int kSize = 96;

// Paint::setAlpha() replaces brush.color.alpha directly with a 0-1 float; it neither divides by
// 255 nor clamps, so every value passed to it must already be a normalized alpha.
constexpr float kHalfAlpha = 0.5f;
constexpr float kTenthAlpha = 26.0f / 255.0f;
constexpr float kUltralowAlpha = 6.0f / 255.0f;
constexpr float kLowAlpha = 51.0f / 255.0f;

const std::array<float, 20> kSwapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                            1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
const std::array<float, 20> kRotateRGB = {0, 1, 0, 0, 0, 0, 0, 1, 0, 0,
                                          1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
// Shifts red up by 0.25, so it maps transparent black to non-transparent: exercises the
// affectsTransparentBlack branch of the color-filter shader path.
const std::array<float, 20> kOffsetRed = {1, 0, 0, 0, 0.25f, 0, 1, 0, 0, 0,
                                          0, 0, 1, 0, 0, 0, 0, 0, 1, 0};

tgfx::Bitmap MakeOpaqueImage() {
  tgfx::Bitmap bitmap = {};
  bitmap.allocPixels(kSize, kSize);
  auto* pixels = static_cast<uint32_t*>(bitmap.lockPixels());
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      pixels[y * kSize + x] = static_cast<uint32_t>(
          (255u << 24) | (static_cast<uint32_t>(x * 2.6f) << 16) |
          (static_cast<uint32_t>(y * 2.6f) << 8) | static_cast<uint32_t>((x + y) * 1.3f));
    }
  }
  bitmap.unlockPixels();
  return bitmap;
}

tgfx::Bitmap MakeAlphaRampImage() {
  tgfx::Bitmap bitmap = {};
  bitmap.allocPixels(kSize, kSize);
  auto* pixels = static_cast<uint32_t*>(bitmap.lockPixels());
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      auto alpha = static_cast<uint32_t>(255 - x * 255 / (kSize - 1));
      // allocPixels() allocates a premultiplied buffer, so each RGB channel must be scaled by
      // its own alpha; the earlier non-premultiplied writes produced alpha=0 pixels with
      // non-zero RGB, which is not a legal input contract.
      pixels[y * kSize + x] =
          (alpha << 24) | ((static_cast<uint32_t>(x * 2.6f) * alpha / 255) << 16) |
          ((static_cast<uint32_t>(y * 2.6f) * alpha / 255) << 8) |
          (static_cast<uint32_t>((x + y) * 1.3f) * alpha / 255);
    }
  }
  bitmap.unlockPixels();
  return bitmap;
}

tgfx::Bitmap MakeSmallImage() {
  tgfx::Bitmap bitmap = {};
  bitmap.allocPixels(kSize / 4, kSize / 4);
  auto* pixels = static_cast<uint32_t*>(bitmap.lockPixels());
  for (int y = 0; y < kSize / 4; ++y) {
    for (int x = 0; x < kSize / 4; ++x) {
      pixels[y * (kSize / 4) + x] = static_cast<uint32_t>(
          (255u << 24) | (static_cast<uint32_t>(x * 12) << 16) |
          (static_cast<uint32_t>(y * 12) << 8) | 96u);
    }
  }
  bitmap.unlockPixels();
  return bitmap;
}

bool WriteScene(const std::string& dir, const std::string& name, const tgfx::Bitmap& bitmap) {
  auto* pixels = bitmap.lockPixels();
  if (pixels == nullptr) {
    return false;
  }
  auto byteCount = static_cast<size_t>(kSize) * kSize * 4;
  std::string path = dir + "/" + name + ".rgba";
  FILE* file = fopen(path.c_str(), "wb");
  if (file == nullptr) {
    bitmap.unlockPixels();
    return false;
  }
  bool ok = fwrite(pixels, 1, byteCount, file) == byteCount;
  fclose(file);
  bitmap.unlockPixels();
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: DiffDriver <output-directory>\n");
    return 2;
  }
  std::string outDir = argv[1];
  auto device = tgfx::GLDevice::Make();
  if (device == nullptr) {
    fprintf(stderr, "FAIL: no GL device\n");
    return 1;
  }
  auto* context = device->lockContext();
  if (context == nullptr) {
    fprintf(stderr, "FAIL: no context\n");
    return 1;
  }
  auto opaqueImage = tgfx::Image::MakeFrom(MakeOpaqueImage());
  auto rampImage = tgfx::Image::MakeFrom(MakeAlphaRampImage());
  auto smallImage = tgfx::Image::MakeFrom(MakeSmallImage());
  if (opaqueImage == nullptr || rampImage == nullptr || smallImage == nullptr) {
    fprintf(stderr, "FAIL: procedural images\n");
    device->unlock();
    return 1;
  }
  auto opaqueShader = tgfx::Shader::MakeImageShader(opaqueImage);
  auto rampShader = tgfx::Shader::MakeImageShader(rampImage);
  std::vector<tgfx::Color> gradientColorsA = {tgfx::Color(1, 0, 0, 1), tgfx::Color(0, 0, 1, 1)};
  std::vector<tgfx::Color> gradientColorsB = {tgfx::Color(0, 1, 0, 1), tgfx::Color(1, 1, 0, 1)};
  auto gradientA =
      tgfx::Shader::MakeLinearGradient(tgfx::Point::Make(0, 0), tgfx::Point::Make(kSize, kSize),
                                       gradientColorsA);
  auto gradientB =
      tgfx::Shader::MakeLinearGradient(tgfx::Point::Make(kSize, 0), tgfx::Point::Make(0, kSize),
                                       gradientColorsB);
  if (opaqueShader == nullptr || rampShader == nullptr || gradientA == nullptr ||
      gradientB == nullptr) {
    fprintf(stderr, "FAIL: shaders\n");
    device->unlock();
    return 1;
  }

  std::vector<std::pair<std::string, std::function<void(tgfx::Canvas*)>>> scenes = {};
  scenes.emplace_back("opaque_colormatrix", [opaqueImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setColorFilter(tgfx::ColorFilter::Matrix(kSwapRedBlue));
    canvas->drawImage(opaqueImage, 0, 0, &paint);
  });
  scenes.emplace_back("alpha_ramp_colormatrix_half", [rampImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kHalfAlpha);
    paint.setColorFilter(tgfx::ColorFilter::Matrix(kSwapRedBlue));
    canvas->drawImage(rampImage, 0, 0, &paint);
  });
  scenes.emplace_back("alpha_ramp_luma_low", [rampImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kTenthAlpha);
    paint.setColorFilter(tgfx::ColorFilter::Luma());
    canvas->drawImage(rampImage, 0, 0, &paint);
  });
  scenes.emplace_back("alpha_ramp_colormatrix_ultralow", [rampImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kUltralowAlpha);
    paint.setColorFilter(tgfx::ColorFilter::Matrix(kSwapRedBlue));
    canvas->drawImage(rampImage, 0, 0, &paint);
  });
  // The blend-mode matrix below pairs two distinct gradients (different colors, opposite sweep
  // directions) at a fixed low paint alpha, so both operands always influence the output; the
  // earlier same-object pairing made both children identical.
  scenes.emplace_back("blend_gradient_gradient_screen", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kLowAlpha);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::Screen, gradientA, gradientB));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  scenes.emplace_back("blend_gradient_gradient_darken", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kLowAlpha);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::Darken, gradientA, gradientB));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  scenes.emplace_back("blend_gradient_gradient_lighten", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kLowAlpha);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::Lighten, gradientA, gradientB));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  scenes.emplace_back("blend_gradient_gradient_colorburn", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kLowAlpha);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::ColorBurn, gradientA, gradientB));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  scenes.emplace_back("blend_gradient_gradient_colordodge", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kLowAlpha);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::ColorDodge, gradientA, gradientB));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  // Nested materialization with both branches visible: the inner blend (two distinct gradients)
  // is the SrcOver foreground at a reduced paint alpha, so the background gradient shows
  // through and the inner result is not masked by an opaque cover.
  scenes.emplace_back("nested_blend_materialization", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(0.6f);
    auto inner = tgfx::Shader::MakeBlend(tgfx::BlendMode::Multiply, gradientA, gradientB);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::SrcOver, gradientA, inner));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  // Accumulated low-alpha overdraw of the quantization-sensitive scene.
  scenes.emplace_back("multilayer_lowalpha_blend", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kLowAlpha);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::Multiply, gradientA, gradientB));
    for (int layer = 0; layer < 5; ++layer) {
      canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
    }
  });
  // A semi-transparent gradient over an opaque image: both operands contribute. The earlier
  // opaque-foreground form let the image fully mask the gradient.
  scenes.emplace_back("blend_gradient_image", [gradientB, opaqueShader](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(0.7f);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::SrcOver, opaqueShader, gradientB));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  scenes.emplace_back("blend_gradient_gradient_lowalpha", [gradientA, gradientB](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(kLowAlpha);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::Multiply, gradientA, gradientB));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  scenes.emplace_back("long_chain_17", [rampImage](tgfx::Canvas* canvas) {
    std::shared_ptr<tgfx::ColorFilter> chain;
    for (int index = 0; index < 17; ++index) {
      chain = tgfx::ColorFilter::Compose(chain, tgfx::ColorFilter::Matrix(kRotateRGB));
    }
    tgfx::Paint paint = {};
    paint.setColorFilter(chain);
    canvas->drawImage(rampImage, 0, 0, &paint);
  });
  scenes.emplace_back("transformed_rotate_scale", [rampImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setColorFilter(tgfx::ColorFilter::Matrix(kSwapRedBlue));
    canvas->save();
    canvas->rotate(15, kSize / 2, kSize / 2);
    canvas->scale(1.3f, 0.8f);
    canvas->drawImage(rampImage, 8, 8, &paint);
    canvas->restore();
  });
  scenes.emplace_back("upscale4x", [smallImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setColorFilter(tgfx::ColorFilter::Matrix(kSwapRedBlue));
    canvas->save();
    canvas->scale(4, 4);
    canvas->drawImage(smallImage, 0, 0, &paint);
    canvas->restore();
  });
  scenes.emplace_back("aa_clip_image", [opaqueImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setColorFilter(tgfx::ColorFilter::Matrix(kSwapRedBlue));
    // A genuinely non-pixel-aligned rect: an axis-aligned integer rect degenerates to a plain
    // scissor with no coverage FP, which would not exercise the AA clip path at all.
    canvas->clipRect(tgfx::Rect::MakeLTRB(8.3f, 7.7f, 87.6f, 88.2f), true);
    canvas->drawImage(opaqueImage, 0, 0, &paint);
  });
  scenes.emplace_back("transparent_black_compose_chain", [rampImage](tgfx::Canvas* canvas) {
    auto inner = tgfx::ColorFilter::Compose(tgfx::ColorFilter::Luma(),
                                             tgfx::ColorFilter::Matrix(kOffsetRed));
    auto composed = tgfx::ColorFilter::Compose(tgfx::ColorFilter::Matrix(kSwapRedBlue), inner);
    tgfx::Paint paint = {};
    paint.setColorFilter(composed);
    canvas->drawImage(rampImage, 0, 0, &paint);
  });
  scenes.emplace_back("blur_drop_shadow", [rampImage](tgfx::Canvas* canvas) {
    auto blur = tgfx::ImageFilter::Blur(6, 6);
    auto shadow = tgfx::ImageFilter::DropShadow(4, 4, 3, 3, tgfx::Color::Black());
    tgfx::Paint paint = {};
    paint.setImageFilter(tgfx::ImageFilter::Compose(blur, shadow));
    canvas->drawImage(rampImage, 0, 0, &paint);
  });

  std::string manifest = "{\"revision\": " + std::to_string(kSceneRevision) +
                         ", \"size\": " + std::to_string(kSize) + ", \"scenes\": [";
  // Steady-state wall time per scene (task 6): each scene renders twice on separate surfaces
  // and only the second render is timed, so first-draw program uploads and compilations do
  // not pollute the comparison between the baseline, runtime, and AOT paths.
  std::string timings = "{";
  int failures = 0;
  for (size_t index = 0; index < scenes.size(); ++index) {
    const auto& scene = scenes[index];
    long long steadyMicros = -1;
    tgfx::Bitmap sceneBitmap = {};
    for (int pass = 0; pass < 2; ++pass) {
      auto surface = tgfx::Surface::Make(context, kSize, kSize);
      if (surface == nullptr) {
        break;
      }
      auto* canvas = surface->getCanvas();
      canvas->clear(tgfx::Color::White());
      auto start = std::chrono::steady_clock::now();
      scene.second(canvas);
      context->flushAndSubmit(true);
      steadyMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      if (pass == 1) {
        if (!sceneBitmap.allocPixels(kSize, kSize)) {
          steadyMicros = -1;
          break;
        }
        auto* pixels = sceneBitmap.lockPixels();
        bool read = pixels != nullptr && surface->readPixels(sceneBitmap.info(), pixels);
        sceneBitmap.unlockPixels();
        if (!read) {
          steadyMicros = -1;
          break;
        }
      }
    }
    if (steadyMicros < 0 || !WriteScene(outDir, scene.first, sceneBitmap)) {
      fprintf(stderr, "FAIL: readback for %s\n", scene.first.c_str());
      ++failures;
      continue;
    }
    manifest += (index == 0 ? "" : ", ") + std::string("\"") + scene.first + "\"";
    timings += (index == 0 ? "" : ", ") + std::string("\"") + scene.first + "\": " +
               std::to_string(steadyMicros);
  }
  manifest += "]}\n";
  FILE* file = fopen((outDir + "/manifest.json").c_str(), "w");
  if (file != nullptr) {
    fputs(manifest.c_str(), file);
    fclose(file);
  }
  timings += "}\n";
  file = fopen((outDir + "/timings.json").c_str(), "w");
  if (file != nullptr) {
    fputs(timings.c_str(), file);
    fclose(file);
  }
  device->unlock();
  if (failures != 0) {
    fprintf(stderr, "FAIL: %d scenes failed\n", failures);
    return 1;
  }
  printf("PASS: %zu scenes written (revision %d)\n", scenes.size(), kSceneRevision);
  return 0;
}
