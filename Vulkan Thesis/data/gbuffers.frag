#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 2, binding = 0) uniform MaterialUBO
{
	int hasAlbedoMap;
	int hasNormalMap;
	int hasSpecularMap;
} material;

layout(set = 2, binding = 1) uniform sampler2D albedoSampler;
layout(set = 2, binding = 2) uniform sampler2D normalSampler;
layout(set = 2, binding = 3) uniform sampler2D specularSampler;

layout(location = 0) in vec3 worldPos;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 color;
layout(location = 3) in vec3 normal;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec4 outNormal;

void main() 
{
	float specular = 0.0;
	vec3 normalTex = vec3(0.5, 0.5, 1.0);
	
	outColor = vec4(1.0, 0.078, 0.576, 1.0);
	outPosition = worldPos;

	if (material.hasAlbedoMap > 0)
		outColor = texture(albedoSampler, texCoord);

	if (material.hasSpecularMap > 0)
		specular = texture(specularSampler, texCoord).r;

	if (material.hasNormalMap > 0) 
		normalTex = texture(normalSampler, texCoord).xyz;

	vec3 N = normalize(normal);
	vec3 T = normalize(tangent);
	vec3 B = cross(N, T);


	mat3 TBN = mat3(T, B, N);
	vec3 onorm = TBN * normalize(normalTex * 2.0 - 1.0);

	outNormal = vec4(onorm, specular);
}