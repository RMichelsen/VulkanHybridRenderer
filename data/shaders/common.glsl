#define ANCHOR_WIDTH 0.001

struct Vertex {
	vec3 pos;
	vec3 normal;
	vec2 uv0;
	vec2 uv1;
};

struct Primitive {
	mat4 transform;
	int vertex_offset;
	int index_offset;
	int index_count;
	int texture_idx;
};

struct DirectionalLight {
	mat4 projview;
	vec4 direction;
	vec4 color;
};

const mat4 SHADOW_BIAS_MATRIX = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0
);
const uint SHADOW_MODE_SPEC_CONST_INDEX = 0;
const uint SHADOW_MODE_RAYTRACED = 0;
const uint SHADOW_MODE_RASTERIZED = 1;
const uint SHADOW_MODE_OFF = 2;
