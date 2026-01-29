#version 300 es

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec4 aColor;
precision mediump float;

out vec4 vColor;

void main() {
  gl_Position = aPos;
  vColor = aColor;
}