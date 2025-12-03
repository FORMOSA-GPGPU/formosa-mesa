/*
  Vertex shader input and uniform
*/
#version 300 es

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMVP;

out vec3 vNormal;

void main() {
  vNormal = aNormal;
  gl_Position = uMVP * aPos;
}