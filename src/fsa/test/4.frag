/*
  Fragment shader with no input
*/
#version 300 es

precision mediump float;

layout(location = 0) out vec4 oColor;

void main() {
  oColor = vec4(1.0f);
}