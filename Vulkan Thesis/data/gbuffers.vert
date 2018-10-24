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

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outColor;
layout(location = 3) out vec3 outNormal;

out gl_PerVertex 
{
    vec4 gl_Position;
};

void main() 
{
	mat4 invTransModel = transpose(inverse(transform.model));
	
	gl_Position = camera.proj * camera.view * transform.model * vec4(inPosition, 1.0);

	outColor = inColor;
	outTexCoord = inTexCoord;
	outNormal = normalize((invTransModel * vec4(inNormal, 0.0)).xyz);
	outPosition = inPosition;
}