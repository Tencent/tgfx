// The chain's leaf and procedural-source coordinate transforms, shared verbatim by the two
// assemblies: the legacy kernel's vertex stage (through the legacy vertex entry) and the declared
// Chain module's vertex hook. The CHAIN_LEAF_SOURCE(k) / CHAIN_SOURCE_COORDS macros select the
// coordinate source per path (the legacy stage defines them from the layout's attributes; the
// declared geometry hooks set v.localCoord instead), so this text is the one definition of the
// transforms and the two paths cannot drift.
#ifndef TGSL_POINTWISE_VERTEX_COORDS_INCLUDED
#define TGSL_POINTWISE_VERTEX_COORDS_INCLUDED

#ifdef TGFX_POINTWISE_SOURCE_COORDS
#if NTEX > 0
  TransformedCoords_0 = (CoordTransformMatrix_1 * vec3(CHAIN_LEAF_SOURCE(0), 1.0)).xyz;
  TransformedCoords_1 = (CoordTransformMatrix_2 * vec3(CHAIN_LEAF_SOURCE(1), 1.0)).xyz;
  TransformedCoords_2 = (CoordTransformMatrix_3 * vec3(CHAIN_LEAF_SOURCE(2), 1.0)).xyz;
  TransformedCoords_3 = (CoordTransformMatrix_4 * vec3(CHAIN_LEAF_SOURCE(3), 1.0)).xyz;
#endif
  SourceCoords = (CoordTransformMatrix_0 * vec3(CHAIN_SOURCE_COORDS, 1.0)).xyz;
#else
#if NTEX > 0
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(CHAIN_LEAF_SOURCE(0), 1.0)).xyz;
  TransformedCoords_1 = (CoordTransformMatrix_1 * vec3(CHAIN_LEAF_SOURCE(1), 1.0)).xyz;
  TransformedCoords_2 = (CoordTransformMatrix_2 * vec3(CHAIN_LEAF_SOURCE(2), 1.0)).xyz;
  TransformedCoords_3 = (CoordTransformMatrix_3 * vec3(CHAIN_LEAF_SOURCE(3), 1.0)).xyz;
#endif
#endif


#endif  // TGSL_POINTWISE_VERTEX_COORDS_INCLUDED
