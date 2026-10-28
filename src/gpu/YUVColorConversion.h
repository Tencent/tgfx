// The YUV-to-RGB conversion tables shared by the runtime texture writer and the texture-source
// feature module, so the precompiled YUV kernels and the runtime-generated shaders receive the
// same matrix bytes.
#pragma once

#include <array>
#include <optional>
#include "tgfx/core/YUVColorSpace.h"

namespace tgfx {

/// The vec4-aligned (12-float) conversion matrix the shaders declare as a mat3, or nullopt for a
/// color space without a table (the shader then keeps its identity conversion).
std::optional<std::array<float, 12>> YUVColorConversionMatrix(YUVColorSpace colorSpace);

}  // namespace tgfx
