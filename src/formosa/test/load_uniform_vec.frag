/*
  Fragment shader with basic arithmetic and input
*/
#version 300 es

// Required in ES frag
precision mediump float;

uniform vec4 uni;

out vec4 oColor;

void main() {
  oColor = uni;
}