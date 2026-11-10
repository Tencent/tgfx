// The declared Chain module's permutation macros, derived from its one joint dimension PROFILE.
// The legacy kernel received TEXTURE_COUNT / HAS_MASK_TEXTURE / RESOURCE_KIND as separate dims;
// the declared assembly folds their seven reachable cells into PROFILE (see modules/Chain.h), and
// this prologue restates the legacy spellings the shared body text reads, so the body stays
// verbatim. CHAIN_SLOT_CAPACITY keeps its contract-macro form: the injected capacities are the
// same C++ constants the runtime uploads from.

//   PROFILE  0 = plain leaf-free        3 = gradient leaf-free        6 = noise four-leaf
//            1 = plain mask             4 = gradient mask
//            2 = plain four-leaf        5 = gradient four-leaf
#if PROFILE == 0 || PROFILE == 3
#define NTEX 0
#define HAS_MASK_TEXTURE 0
#elif PROFILE == 1 || PROFILE == 4
#define NTEX 0
#define HAS_MASK_TEXTURE 1
#else
#define NTEX 4
#define HAS_MASK_TEXTURE 0
#endif

#if PROFILE == 2 || PROFILE == 5
#define RESOURCE_KIND 1
#elif PROFILE == 6
#define RESOURCE_KIND 2
#else
#define RESOURCE_KIND 0
#endif

#if RESOURCE_KIND == 2
#define CHAIN_SLOT_CAPACITY TGFX_NOISE_SLOT_CAPACITY
#else
#define CHAIN_SLOT_CAPACITY TGFX_CHAIN_SLOT_CAPACITY
#endif

// The resource kinds carry the SourceCoords varying and reserve CoordTransformMatrix_0 for the
// source module (the legacy HAS_SOURCE_COORDS vertex dim, mirrored from RESOURCE_KIND).
#if RESOURCE_KIND != 0
#define TGFX_POINTWISE_SOURCE_COORDS 1
#endif
