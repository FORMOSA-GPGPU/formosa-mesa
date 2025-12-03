/*
  Vertex shader with two functions
*/
#version 300 es

layout(location = 0) in vec4 aPos;

vec4 calc(vec4 a, vec4 b) {
  return a + b * 2.0f;
}

void main() {
  gl_Position = calc(aPos, aPos.xxxx);
}