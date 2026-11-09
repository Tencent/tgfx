// ShapeInstancedGeometry vertex hook: the local coordinate through the uv matrix, the device
// position through the view matrix plus the instance offset, and the sampling coordinate through
// the FP tree's transform. The expressions are verbatim from the former
// kernels/shape_instanced_fill.vert.
void ShapeInstancedGeometry_vertex(inout Vertex v) {
  highp vec2 local = (ShapeInstancedGeometry_UVMatrix * vec3(ShapeInstancedGeometry_Position, 1.0)).xy;
  highp vec2 position =
      (ShapeInstancedGeometry_ViewMatrix * vec3(ShapeInstancedGeometry_Position, 1.0)).xy +
      ShapeInstancedGeometry_Offset;
  v.position = position;
  v.localCoord = local;
  ShapeInstancedGeometry_vCoord =
      (ShapeInstancedGeometry_CoordMatrix * vec3(local, 1.0)).xy;
  ShapeInstancedGeometry_vCoverage = ShapeInstancedGeometry_Coverage;
  ShapeInstancedGeometry_vColor = ShapeInstancedGeometry_InstanceColor;
}
