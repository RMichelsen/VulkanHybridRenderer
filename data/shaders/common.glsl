#define ANCHOR_WIDTH 0.001

struct Vertex {
	vec3 pos;
	vec3 normal;
	vec4 tangent;
	vec2 uv0;
	vec2 uv1;
};

struct Material {
	int base_color_texture;
	int normal_map;
	int alpha_mask;
	float alpha_cutoff;
};

struct Primitive {
	mat4 transform;
	Material material;
	uint vertex_offset;
	uint index_offset;
	uint index_count;
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
