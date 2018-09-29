#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform SceneObjectUBO
{
	mat4 model;
} transform;

layout(set = 1, binding = 0) uniform CameraUBO
{
	mat4 view;
	mat4 proj;
} camera;

layout(set = 2, binding = 0) uniform MaterialUBO
{
	int hasAlbedoMap;
	int hasNormalMap;
} material;

layout(set = 2, binding = 1) uniform sampler2D albedoSampler;
layout(set = 2, binding = 2) uniform sampler2D normalSampler;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
// layout(location = 3) in vec3 worldPos;

layout(location = 0) out vec4 outColor;

void main() 
{
	vec3 diffuse;
	if (material.hasAlbedoMap > 0)
		diffuse = texture(albedoSampler, texCoord).rgb;
	else
		diffuse = vec3(1.0);

	// TODO normal

	outColor = vec4(diffuse, 1.0);
}