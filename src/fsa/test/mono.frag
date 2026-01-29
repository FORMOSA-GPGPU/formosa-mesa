/*
  Fragment shader with const red output
*/
#version 300 es
precision mediump float;

out vec4 oColor;

void main() {
  oColor = vec4(1.0f, 0.0f, 0.0f, 0.0f);
}