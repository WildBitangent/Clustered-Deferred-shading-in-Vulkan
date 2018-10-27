#version 450
#extension GL_ARB_separate_shader_objects : enable

// layout(push_constant) uniform PushConstants 
// {
// } pushConstants;

layout(set = 0, binding = 0) uniform CameraUBO
{
	mat4 view;
	mat4 proj;
	vec3 position;
} camera;

layout(set = 1, binding = 0) uniform Model
{
	mat4 model;
} transform;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec3 inTangent;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outColor;
layout(location = 3) out vec3 outNormal;
layout(location = 4) out vec3 outTangent;
layout(location = 5) out vec3 outBitangent;

out gl_PerVertex 
{
    vec4 gl_Position;
};

void main() 
{
	mat3 invTransModel = transpose(inverse(mat3(transform.model)));
	
	gl_Position = camera.proj * camera.view * transform.model * vec4(inPosition, 1.0);

	outColor = inColor;
	outTexCoord = inTexCoord;

	vec3 N = normalize(invTransModel * inNormal);
	vec3 T = normalize(invTransModel * inTangent);
	// T = normalize(T - dot(T, N) * N);
	vec3 B = cross(N, T);

	outNormal = N;
	outTangent = T;
	outBitangent = B;

	outPosition = (camera.view * transform.model * vec4(inPosition, 1.0)).rgb;
}