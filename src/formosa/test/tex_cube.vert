#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

out vec3 vNormal;
out vec2 vTexCoord;

uniform mat4 uMVP;
uniform mat4 uModel;

void main() {
  vNormal = mat3(uModel) * aNormal;
  vTexCoord = aTexCoord;
  gl_Position = uMVP * vec4(aPos, 1.0f);
}