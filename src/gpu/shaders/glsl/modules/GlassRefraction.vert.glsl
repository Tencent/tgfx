// GlassRefraction vertex hook: maps the geometry's local coordinate (the raw position for both
// served geometries) through the background-coordinate transform. Affine by contract (the matcher
// rejects a projective background transform), so the plain vec2 interpolation reproduces the
// legacy kernel's varying exactly — dividing an interpolated z is NOT bit-identical.
void GlassRefraction_vertex(inout Vertex v) {
  GlassRefraction_vCoord = (GlassRefraction_CoordMatrix * vec3(v.localCoord, 1.0)).xy;
}
