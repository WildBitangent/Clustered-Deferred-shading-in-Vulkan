#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform DebugUBO
{
	uint index;
} state;


layout(set = 1, binding = 3) uniform sampler2D samplerposition;
layout(set = 1, binding = 4) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 5) uniform sampler2D samplerNormal;
layout(set = 1, binding = 6) uniform sampler2D samplerDepth;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outFragcolor;


void main() 
{
	// Get G-Buffer values
	vec3 ret[5];

	ret[0] = texture(samplerAlbedo, inUV).rgb;
	ret[1] = vec3(texture(samplerNormal, inUV).rg, 0);
	ret[2] = vec3(texture(samplerposition, inUV).a);
	ret[3] = texture(samplerposition, inUV).rgb;
	ret[4] = vec3(texture(samplerDepth, inUV).r);
	
 	outFragcolor = vec4(ret[state.index - 1], 1.0);	
}