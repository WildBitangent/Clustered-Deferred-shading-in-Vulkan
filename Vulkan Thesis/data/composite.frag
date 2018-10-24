#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraUBO
{
	mat4 view;
	mat4 proj;
	vec3 position;
} camera;


layout(set = 1, binding = 2) uniform sampler2D samplerposition;
layout(set = 1, binding = 3) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 4) uniform sampler2D samplerNormal;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outFragcolor;

// struct Light {
// 	vec4 position;
// 	vec3 color;
// 	float radius;
// };


void main() 
{
	// Get G-Buffer values
	vec3 fragPos = texture(samplerposition, inUV).rgb;
	vec4 albedo = texture(samplerAlbedo, inUV);
	vec3 normal = texture(samplerNormal, inUV).rgb;
	float specular = texture(samplerNormal, inUV).a;
	
	#define ambient 0.3
	
	// Ambient part
	vec3 fragcolor = albedo.rgb * ambient;
	

	vec3 light = vec3(0.7);

	// // Diffuse part
	// vec3 diff = albedo.rgb * max(0.0, dot(normal, light));
	// fragcolor += diff;

	// // Specular part
	// vec3 R = reflect(-light, normal);
	// float NdotR = max(0.0, dot(R, V));
	// vec3 spec = albedo.a * pow(NdotR, 16.0) * atten;

   
 	outFragcolor = vec4(fragcolor, 1.0);	
}