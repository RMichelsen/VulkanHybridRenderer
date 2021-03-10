#define PI 3.1415926538

struct DirectionalLight {
	mat4 projview;
	vec4 direction;
	vec4 color;
};

struct PerFrameData {
	mat4 camera_view;
	mat4 camera_proj;
	mat4 camera_view_inverse;
	mat4 camera_proj_inverse;
	DirectionalLight directional_light;
	uint frame_index;
};

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

// Uniformly sample rays in a cone
// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations.html#UniformSampleCone
vec3 uniform_sample_cone(vec2 u, float cos_theta_max) {
	float cos_theta = (1.0 - u.x) + u.x * cos_theta_max;
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	float phi = u.y * 2.0 * PI;
	return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

// Random Number Generation
// https://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
// Create an initial random number for this thread
uint seed_thread(uint seed) {
	// Thomas Wang hash 
	// Ref: http://www.burtleburtle.net/bob/hash/integer.html
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}
// Generate a random 32-bit integer
uint random(inout uint state) {
	// Xorshift algorithm from George Marsaglia's paper.
	state ^= (state << 13);
	state ^= (state >> 17);
	state ^= (state << 5);
	return state;
}
// Generate a random float in the range [0.0f, 1.0f)
float random01(inout uint state) {
	return uintBitsToFloat(0x3f800000 | random(state) >> 9) - 1.0;
}
// Generate a random float in the range [0.0f, 1.0f]
float random01_inclusive(inout uint state) {
	return random(state) / float(0xffffffff);
}
// Generate a random integer in the range [lower, upper]
uint random(inout uint state, uint lower, uint upper) {
	return lower + uint(float(upper - lower + 1) * random01(state));
}

// Create an orthonormal basis given a unit vector
// https://backend.orbit.dtu.dk/ws/portalfiles/portal/126824972/onb_frisvad_jgt2012_v2.pdf
mat3x3 onb_from_unit_vector(vec3 n) {
	mat3x3 M;
	M[2] = n;
	if(n.z < -0.9999999) {
		M[0] = vec3(0.0, -1.0, 0.0);
		M[1] = vec3(-1.0, 0.0, 0.0);
		return M;
	}
	float a = 1.0 / (1.0 + n.z);
	float b = -n.x * n.y * a;
	M[0] = vec3(1.0 - n.x * n.x * a, b, -n.x);
	M[1] = vec3(b, 1.0 - n.y * n.y * a, -n.y);
	return M;
}
