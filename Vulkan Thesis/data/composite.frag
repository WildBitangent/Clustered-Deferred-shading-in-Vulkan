#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

// #define TILE_SIZE 32 // TODO bake it with compilation or w/e
#define PAGE_SIZE 512
#define NEAR 0.01
#define FAR 100.0
// #define Y_SLICES 0.083381608939051058394765834642179160608822393696599701826

layout (constant_id = 0) const uint TILE_SIZE = 0;
layout (constant_id = 1) const float Y_SLICES = 0.0;

// --- structs ---
#include "structs.inl"

// --- layouts ---
layout(set = 0, binding = 0) uniform CameraUBO
{
	mat4 view;
	mat4 proj;
	mat4 invProj;
	vec3 position;
	uvec2 screenSize;
} camera;

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

// Returns ±1
vec2 signNotZero(vec2 v) 
{
	return vec2((v.x >= 0.0) ? 1.0 : -1.0, (v.y >= 0.0) ? 1.0 : -1.0);
}

vec3 oct_to_float32x3(vec2 e) 
{
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));

	if (v.z < 0) 
		v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);

	return normalize(v);
}

uint addressTranslate(uint virtualAddress)
{
	uint pageNumber = virtualAddress / PAGE_SIZE;
	uint pageAddress = (table.nodes[pageNumber] - 1) * PAGE_SIZE;
	uint offset = virtualAddress % PAGE_SIZE;

	return pageAddress + offset;
}

uint packKey(uvec3 key)
{
	return key.x | key.y << 7 | (key.z & 0x3FF) << 14;
}

float getViewDepth(float projDepth)
{
	float normalizedProjDepth = projDepth * 2.0 - 1.0;
	// return 1.0 / (normalizedProjDepth * camera.invProj[2][3] + camera.invProj[3][3]);
	return camera.proj[3][2] / (normalizedProjDepth + camera.proj[2][2]);
}

void main() 
{
	// Get G-Buffer values
	vec3 fragPos = texture(samplerPosition, inUV).rgb;
	vec4 albedo = texture(samplerAlbedo, inUV);
	vec3 normal = oct_to_float32x3(texture(samplerNormal, inUV).rg);
	float specStrength = texture(samplerPosition, inUV).a;

	float depth = getViewDepth(texture(samplerDepth, inUV).r);
	uint k = uint(log(depth / NEAR) / Y_SLICES); // 72 slices along Y
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
			vec3 R = reflect(-L, N);
			float spec = max(0.0, dot(R, V));
			vec3 specular = vec3(specStrength * pow(spec, 16.0)) * atten * light.intensity;

			fragcolor += specular + diff;
		}
	}

 	outFragcolor = vec4(fragcolor, 1.0);	
}
