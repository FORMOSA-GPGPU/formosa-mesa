/*
  Vertex Shader with if-else blocks and vec4 swizzle
*/
#version 300 es

layout(location = 0) in vec4 aPos;

void main() {
  if (aPos.y > aPos.x) {
    gl_Position = vec4(aPos.xyz, 1.0f);
  } else {
    gl_Position = vec4(aPos.zyx, 1.0f);
  }
}