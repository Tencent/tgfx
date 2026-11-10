// GlassLocalMask vertex hook: maps the quad's device-space position through the mask's own
// transform. Affine by contract (the matcher rejects a projective mask transform), so the plain
// vec2 interpolation reproduces the legacy kernel's TransformedCoords_1 exactly — dividing an
// interpolated z is NOT bit-identical for an affine matrix.
void GlassLocalMask_vertex(inout Vertex v) {
  GlassLocalMask_vCoord = (GlassLocalMask_CoordMatrix * vec3(v.localCoord, 1.0)).xy;
}
