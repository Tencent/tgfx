// Extended pointwise graphs: resource layout and transfer mechanism are compile-time profiles.
#version 450

#ifndef RESOURCE_KIND
#define RESOURCE_KIND 0
#endif
#if RESOURCE_KIND == 0
#define TGFX_POINTWISE_NOISE 1
#else
#define TGFX_POINTWISE_TILED 1
#define TGFX_POINTWISE_GRADIENT 1
#endif
#define TGFX_POINTWISE_SOURCE_COORDS 1

#include "pointwise_fragment.inc"
