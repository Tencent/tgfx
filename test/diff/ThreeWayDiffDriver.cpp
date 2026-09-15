// Three-way diff driver (audit F02 / tasks 5-6). Renders a fixed scene matrix through the
// public rendering API only, so the exact same source builds against the merge-base baseline
// (no AOT code at all), the branch with TGFX_AOT_DISABLE set (pure runtime route, no
// materialization), and the branch with AOT fully enabled. Each scene is written as one raw
// RGBA_8888 file plus a manifest; test/diff/compare_scenes.py computes the per-scene
// differences. Bump kSceneRevision whenever a scene is added or changed so stale output
// directories are rejected instead of compared.
#include <array>
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

constexpr int kSceneRevision = 1;
constexpr int kSize = 96;

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
      pixels[y * kSize + x] =
          (alpha << 24) | (static_cast<uint32_t>(x * 2.6f) << 16) |
          (static_cast<uint32_t>(y * 2.6f) << 8) | static_cast<uint32_t>((x + y) * 1.3f);
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
  std::vector<tgfx::Color> gradientColors = {tgfx::Color(1, 0, 0, 1), tgfx::Color(0, 0, 1, 1)};
  auto gradient =
      tgfx::Shader::MakeLinearGradient(tgfx::Point::Make(0, 0), tgfx::Point::Make(kSize, kSize),
                                       gradientColors);
  if (opaqueShader == nullptr || rampShader == nullptr || gradient == nullptr) {
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
    paint.setAlpha(128);
    paint.setColorFilter(tgfx::ColorFilter::Matrix(kSwapRedBlue));
    canvas->drawImage(rampImage, 0, 0, &paint);
  });
  scenes.emplace_back("alpha_ramp_luma_low", [rampImage](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(26);
    paint.setColorFilter(tgfx::ColorFilter::Luma());
    canvas->drawImage(rampImage, 0, 0, &paint);
  });
  scenes.emplace_back("blend_gradient_image", [gradient, opaqueShader](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::SrcOver, gradient, opaqueShader));
    canvas->drawRect(tgfx::Rect::MakeWH(kSize, kSize), paint);
  });
  scenes.emplace_back("blend_gradient_gradient_lowalpha", [gradient](tgfx::Canvas* canvas) {
    tgfx::Paint paint = {};
    paint.setAlpha(51);
    paint.setShader(tgfx::Shader::MakeBlend(tgfx::BlendMode::Multiply, gradient, gradient));
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
    canvas->clipRect(tgfx::Rect::MakeLTRB(8, 8, kSize - 8, kSize - 8), true);
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
  int failures = 0;
  for (size_t index = 0; index < scenes.size(); ++index) {
    const auto& scene = scenes[index];
    auto surface = tgfx::Surface::Make(context, kSize, kSize);
    if (surface == nullptr) {
      fprintf(stderr, "FAIL: surface for %s\n", scene.first.c_str());
      ++failures;
      continue;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(tgfx::Color::White());
    scene.second(canvas);
    context->flushAndSubmit(true);
    tgfx::Bitmap bitmap = {};
    if (!bitmap.allocPixels(kSize, kSize)) {
      ++failures;
      continue;
    }
    auto* pixels = bitmap.lockPixels();
    bool read = pixels != nullptr && surface->readPixels(bitmap.info(), pixels);
    bitmap.unlockPixels();
    if (!read || !WriteScene(outDir, scene.first, bitmap)) {
      fprintf(stderr, "FAIL: readback for %s\n", scene.first.c_str());
      ++failures;
      continue;
    }
    manifest += (index == 0 ? "" : ", ") + std::string("\"") + scene.first + "\"";
  }
  manifest += "]}\n";
  FILE* file = fopen((outDir + "/manifest.json").c_str(), "w");
  if (file != nullptr) {
    fputs(manifest.c_str(), file);
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
