#version 300 es
precision mediump float;

in vec3 vNormal;
uniform vec3 uLightDir;

out vec4 fragColor;

void main() {
	vec3 norm = normalize(vNormal);
	vec3 lightDir = normalize(uLightDir);
	float diff = max(dot(norm, lightDir), 0.0);
	vec3 color = vec3(1.0, 0.0, 0.0);
	
	vec3 diffuse = diff * color;
	vec3 ambient = 0.3 * color;

	fragColor = vec4(ambient + diffuse, 1.0);
}