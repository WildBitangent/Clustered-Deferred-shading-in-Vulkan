struct Light
{
	vec3 position;
	float radius;
	vec3 intensity;
	uint mortonCode;
};

struct Key
{
	uint mortonCode;
	uint lightIndex;
};

struct Node
{
	vec3 min;
	vec3 max;
};

struct LevelParam
{
	uint count;
	uint offset;
};

struct ViewFrustum
{
	vec4 plane[6];
	vec3 point[8];
};

struct AABB
{
	vec3 min;
	vec3 max;
};