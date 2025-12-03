/*
  Fragment shader with basic arithmetic and input
*/
#version 300 es

// Required in ES frag
precision mediump float;

in vec3 vNormal;

layout(location = 0) out vec4 oColor;

void main() {
  vec3 normal_color = vNormal * 0.5 + 0.5;
  oColor = vec4(normal_color, 1.0);
}