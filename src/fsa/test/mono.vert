/*
  Vertex shader that does no transformation
*/
#version 300 es

layout(location = 0) in vec3 aPos;
precision mediump float;

void main() {
  gl_Position = vec4(aPos, 1.0f);
}