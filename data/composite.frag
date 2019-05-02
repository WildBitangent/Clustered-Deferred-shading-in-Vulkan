#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout (constant_id = 0) const uint TILE_SIZE = 0;
layout (constant_id = 1) const float Y_SLICES = 0.0;

// --- structs ---
#include "structs.inl"

// --- layouts ---
layout(std430, set = 1, binding = 0) buffer readonly PointLights
{
	Light lights[];
} pointLights;

layout(std430, set = 1, binding = 1) buffer readonly LightsOut
{
	uint count;
	uint data[];
} lightsOut;

layout(set = 1, binding = 3) uniform sampler2D samplerPosition;
layout(set = 1, binding = 4) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 5) uniform sampler2D samplerNormal;
layout(set = 1, binding = 6) uniform sampler2D samplerDepth;

layout(std430, set = 1, binding = 7) buffer readonly PageTable
{
	uint counter;
	uint pad0; // pad for indirect dispatch
	uint pad1;

	uint nodes[];
} table;

layout(std430, set = 1, binding = 8) buffer readonly PagePool
{
	uint data[];
} pool;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFragcolor;

#include "page_utils.comp"


void main() 
{
	// Get G-Buffer values
	vec3 fragPos = texture(samplerPosition, inUV).rgb;
	vec4 albedo = texture(samplerAlbedo, inUV);
	vec3 normal = octToFloat32x3(texture(samplerNormal, inUV).rg);
	float specStrength = texture(samplerPosition, inUV).a;

	float depth = getViewDepth(texture(samplerDepth, inUV).r);
	uint k = uint(log(depth / NEAR) / Y_SLICES);
	uvec3 key = uvec3(uvec2(gl_FragCoord.xy) / uvec2(TILE_SIZE, TILE_SIZE), k);
	uint address = addressTranslate(packKey(key));
	uint index = pool.data[address];
	
	// Ambient part
	// #define ambient 0.03
	#define ambient 0.25

	vec3 fragcolor = albedo.rgb * ambient;

	uint indirectCount = lightsOut.data[index];
	for (uint ii = 0; ii < indirectCount; ii++)
	{
		uint stop = (ii == indirectCount - 1) ? lightsOut.data[index + 1] : 192;
		uint offset = lightsOut.data[index + ii + 2];

		for (uint i = 0; i < stop; i++)
		{
			uint lightIndex = lightsOut.data[offset + i];

			Light light = pointLights.lights[lightIndex];

			vec3 L = light.position - fragPos;
			vec3 V = normalize(-fragPos);
			vec3 N = normalize(normal);

			// Attenuation
			float atten = clamp(1.0 - pow(length(L), 2.0) / pow(light.radius, 2.0), 0.0, 1.0);
			L = normalize(L);

			// Diffuse part
			vec3 diff = albedo.rgb * max(0.0, dot(N, L)) * atten * light.intensity;

			// Specular part
			vec3 H = normalize(L + V);
			float spec = max(0.0, dot(N, H));
			vec3 specular = vec3(specStrength * pow(spec, 16.0)) * atten * light.intensity;

			fragcolor += specular + diff;
		}
	}

 	outFragcolor = vec4(fragcolor, 1.0);	
}
