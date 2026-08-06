// QuadConstColorShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + ConstColorProcessor + EmptyXferProcessor
// InputMode is a runtime uniform: 0=Ignore, 1=ModulateRGBA, 2=ModulateA.
#version 450

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 ConstColor;
  int InputMode;
};

layout(location = 0) in float vCoverage;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 color = ConstColor;
  if (InputMode == 1) {
    color *= vColor;
  } else if (InputMode == 2) {
    color *= vColor.a;
  }
  fragColor = color * vCoverage;
}
