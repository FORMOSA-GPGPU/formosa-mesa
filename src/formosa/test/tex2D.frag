#version 300 es
precision mediump float;

in vec2 vTexCoord;
out vec4 oColor;

uniform sampler2D uTex;

void main() {
  oColor = texture(uTex, vTexCoord);
}