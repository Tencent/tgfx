void BlurSourceGeometry_vertex(inout Vertex v) {
  v.position = (BlurSourceGeometry_Matrix * vec3(BlurSourceGeometry_Position, 1.0)).xy;
  BlurSourceGeometry_vCoord =
      (BlurSourceGeometry_CoordMatrix * vec3(BlurSourceGeometry_Position, 1.0)).xy;
}
