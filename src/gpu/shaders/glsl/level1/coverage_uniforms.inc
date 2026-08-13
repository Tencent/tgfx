// Coverage uniform declarations — include inside FragmentUniformBlock.
// HAS_RUNTIME_CLIP keeps AARect coverage in a runtime branch; older shader families continue using
// HAS_COVERAGE until they are migrated to the same resource-interface contract.
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
#elif HAS_COVERAGE
  vec4 Rect;
#endif

#if HAS_RUNTIME_CLIP
  #if HAS_RUNTIME_DEVICE_MASK
    mat3 DeviceCoordMatrix;
    int HasDeviceMask;
  #elif HAS_DEVICE_MASK
    mat3 DeviceCoordMatrix;
  #endif
#elif HAS_COVERAGE == 2
  mat3 DeviceCoordMatrix;
#endif
