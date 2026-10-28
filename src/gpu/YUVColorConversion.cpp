// See YUVColorConversion.h.
#include "gpu/YUVColorConversion.h"

namespace tgfx {
namespace {

constexpr float ColorConversion601LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.391762f, 2.017232f, 1.596027f, -0.812968f, 0.0f,
};

constexpr float ColorConversion601FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772f, 1.402f, -0.714136f, 0.0f,
};

constexpr float ColorConversion709LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.213249f, 2.112402f, 1.792741f, -0.532909f, 0.0f,
};

constexpr float ColorConversion709FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.187324f, 1.8556f, 1.5748f, -0.468124f, 0.0f,
};

constexpr float ColorConversion2020LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.187326f, 2.141772f, 1.678674f, -0.650424f, 0.0f,
};

constexpr float ColorConversion2020FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.164553f, 1.8814f, 1.4746f, -0.571353f, 0.0f,
};

constexpr float ColorConversionJPEGFullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772000f, 1.402f, -0.714136f, 0.0f,
};

std::array<float, 12> AlignMat3(const float* mat3) {
  // clang-format off
  return {
    mat3[0], mat3[1], mat3[2], 0.0f,
    mat3[3], mat3[4], mat3[5], 0.0f,
    mat3[6], mat3[7], mat3[8], 0.0f
  };
  // clang-format on
}

}  // namespace

std::optional<std::array<float, 12>> YUVColorConversionMatrix(YUVColorSpace colorSpace) {
  switch (colorSpace) {
    case YUVColorSpace::BT601_LIMITED:
      return AlignMat3(ColorConversion601LimitRange);
    case YUVColorSpace::BT601_FULL:
      return AlignMat3(ColorConversion601FullRange);
    case YUVColorSpace::BT709_LIMITED:
      return AlignMat3(ColorConversion709LimitRange);
    case YUVColorSpace::BT709_FULL:
      return AlignMat3(ColorConversion709FullRange);
    case YUVColorSpace::BT2020_LIMITED:
      return AlignMat3(ColorConversion2020LimitRange);
    case YUVColorSpace::BT2020_FULL:
      return AlignMat3(ColorConversion2020FullRange);
    case YUVColorSpace::JPEG_FULL:
      return AlignMat3(ColorConversionJPEGFullRange);
    default:
      return std::nullopt;
  }
}

}  // namespace tgfx
