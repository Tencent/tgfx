// Coverage uniform declarations — include inside FragmentUniformBlock.
// HAS_RUNTIME_CLIP selects the runtime clip contract (Rect, HasClip and the rrect fields); every
// includer enables it, and HAS_RUNTIME_DEVICE_MASK / HAS_DEVICE_MASK add the device mask fields.
#ifndef HAS_RUNTIME_CLIP
#define HAS_RUNTIME_CLIP 0
#endif
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif

#ifndef HAS_RUNTIME_DEVICE_MASK
#define HAS_RUNTIME_DEVICE_MASK 0
#endif

#if HAS_RUNTIME_CLIP
  vec4 Rect;
  int HasClip;
#include "coverage/rrect_clip_uniforms.inc"
#endif

#if HAS_RUNTIME_CLIP
  #if HAS_RUNTIME_DEVICE_MASK
    mat3 DeviceCoordMatrix;
    int HasDeviceMask;
  #elif HAS_DEVICE_MASK
    mat3 DeviceCoordMatrix;
  #endif
#endif
