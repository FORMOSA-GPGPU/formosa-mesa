/*
  Fragment shader with constant value
*/
#version 300 es

// Required in ES frag
precision mediump float;

layout(location = 0) out vec4 oColor;

void main() {
  oColor = vec4(1.0f);
}