#version 300 es
precision mediump float;

in vec3 vNormal;
in vec2 vTexCoord;

uniform vec3 uLightDir;
uniform sampler2D uTex;

out vec4 fragColor;

void main() {
	vec3 norm = normalize(vNormal);
	vec3 lightDir = normalize(uLightDir);
	float diff = max(dot(norm, lightDir), 0.0);
	vec3 color = texture(uTex, vTexCoord).xyz;
	// vec3 color = vec3(1.0, 0.0, 0.0);
	
	vec3 diffuse = diff * color;
	vec3 ambient = 0.3 * color;

	fragColor = vec4(ambient + diffuse, 1.0);
}