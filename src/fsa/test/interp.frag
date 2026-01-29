#version 300 es

// Required in ES frag
precision mediump float;

in vec4 vColor;

out vec4 oColor;

void main() {
  oColor = vColor;
}