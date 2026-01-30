#version 300 es

// Required in ES frag
precision mediump float;

in vec3 vColor;

out vec4 oColor;

void main() {
  oColor = vec4(vColor, 1.0);
}