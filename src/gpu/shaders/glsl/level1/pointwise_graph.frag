// Extended pointwise graphs: resource layout and transfer mechanism are compile-time profiles.
#version 450

#ifndef RESOURCE_KIND
#define RESOURCE_KIND 0
#endif
#if RESOURCE_KIND == 0
#define TGFX_POINTWISE_NOISE 1
#else
#define TGFX_POINTWISE_TILED 1
#endif
#define GP_LAYOUT 0
#define HAS_COVERAGE 1
#define HAS_COLOR 0
#define TEXTURE_COUNT 1
#define HAS_MASK_TEXTURE 0

#include "pointwise_fragment.inc"
