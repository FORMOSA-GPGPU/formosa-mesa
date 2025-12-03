/*
  Vertex Shader with alu add
*/
#version 300 es

layout(location = 0) in vec4 aPos;

void main() {
  gl_Position = aPos + vec4(1.0, 2.0, 3.0, 4.0);
}