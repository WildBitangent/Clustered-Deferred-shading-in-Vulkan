#version 450
#extension GL_ARB_separate_shader_objects : enable

#define MAX_TILE_LIGHTS 1024

layout (constant_id = 0) const uint TILE_SIZE = 0;

// --- structs ---
struct Light
{
	vec3 position;
	float radius;
	vec3 intensity;
	float pad;
};

struct VisibleLights
{
	uint count;
	uint lights[MAX_TILE_LIGHTS];
};

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

layout(std430, set = 1, binding = 1) buffer readonly TileLights
{
	VisibleLights visibleLights[];
} tileLights;

layout(set = 1, binding = 3) uniform sampler2D samplerPosition;
layout(set = 1, binding = 4) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 5) uniform sampler2D samplerNormal;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFragcolor;

// Returns ±1
vec2 signNotZero(vec2 v) 
{
	return vec2((v.x >= 0.0) ? 1.0 : -1.0, (v.y >= 0.0) ? 1.0 : -1.0);
}

vec3 octToFloat32x3(vec2 e) 
{
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));

	if (v.z < 0) 
		v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);

	return normalize(v);
}

void main() 
{
	// Get G-Buffer values
	vec3 fragPos = texture(samplerPosition, inUV).rgb;
	vec4 albedo = texture(samplerAlbedo, inUV);
	vec3 normal = octToFloat32x3(texture(samplerNormal, inUV).rg);
	float specStrength = texture(samplerPosition, inUV).a;

	uvec2 tileID = uvec2(gl_FragCoord.xy) / uvec2(TILE_SIZE, TILE_SIZE);
	uint index = tileID.y * ((camera.screenSize.x - 1) / TILE_SIZE + 1) + tileID.x;

	// Ambient part
	#define ambient 0.25
	
	vec3 fragcolor = albedo.rgb * ambient;

	for (uint i = 0; i < tileLights.visibleLights[index].count; i++)
	{
		uint lightIndex = tileLights.visibleLights[index].lights[i];

		Light light = pointLights.lights[lightIndex];
		light.position = (camera.view * vec4(light.position, 1.0)).xyz;

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

 	outFragcolor = vec4(fragcolor, 1.0);	
}