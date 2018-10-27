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
	float specStrength = texture(samplerNormal, inUV).a;
	
	#define ambient 0.3
	vec3 lightDir = vec3(0.7, 0.7, 0.6);
	// vec3 lightDir = normalize(vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]));
	vec3 camPos = -vec3(camera.view[0][3], camera.view[1][3], camera.view[2][3]);
	
	// Ambient part
	vec3 fragcolor = albedo.rgb * ambient;


	// Diffuse part
	vec3 diff = albedo.rgb * max(0.0, dot(normal, lightDir));
	fragcolor += diff;

	// Specular part
	vec3 viewDir = normalize(camPos - fragPos);
	vec3 reflectDir = reflect(-lightDir, normal);

	float spec = max(0.0, dot(viewDir, reflectDir));
	vec3 specular = vec3(specStrength * pow(spec, 16.0));
	fragcolor += specular;
   
 	outFragcolor = vec4(fragcolor, 1.0);	
}