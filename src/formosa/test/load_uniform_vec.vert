/*
  load uniform vec4
*/
#version 300 es

precision mediump float;
uniform vec4 uni;

void main() {
  gl_Position = uni;
}