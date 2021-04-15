#define COS_PI_4		0.70710678118654752440084
#define PI				3.14159265358979323846264
#define TWO_PI			6.28318530717958647692528
#define PI_INVERSE		0.31830988618379067153776

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

const uint AMBIENT_OCCLUSION_MODE_SPEC_CONST_INDEX = 1;
const uint AMBIENT_OCCLUSION_MODE_RAYTRACED = 0;
const uint AMBIENT_OCCLUSION_MODE_SSAO = 1;
const uint AMBIENT_OCCLUSION_MODE_OFF = 2;

const uint REFLECTION_MODE_SPEC_CONST_INDEX = 2;
const uint REFLECTION_MODE_RAYTRACED = 0;
const uint REFLECTION_MODE_SSR = 1;
const uint REFLECTION_MODE_OFF = 2;

// Uniformly sample rays in a cone
// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations.html#UniformSampleCone
vec3 uniform_sample_cone(vec2 u, float cos_theta_max) {
	float cos_theta = (1.0 - u.x) + u.x * cos_theta_max;
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	float phi = u.y * TWO_PI;
	return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

// Uniformly sample rays on a cosine-weighted hemisphere
vec3 uniform_sample_cosine_weighted_hemisphere(vec2 u) {
	float x = sqrt(u.x) * cos(TWO_PI * u.y);
	float y = sqrt(u.x) * sin(TWO_PI * u.y);
	float z = sqrt(1 - u.x);
	return vec3(x, y, z);
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

// A survey of efficient representations for independent unit vectors [JCGT]
// Fast octahedral encoding 
vec2 sign_not_zero(vec2 v) {
	return vec2((v.x >= 0.0) ? 1.0 : -1.0, (v.y >= 0.0) ? 1.0 : -1.0);
}
vec2 vec3_encode_to_oct(vec3 v) {
	vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
	return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * sign_not_zero(p)) : p;
}
vec3 oct_decode_to_vec3(vec2 e) {
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if(v.z < 0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
	return normalize(v);
}

// Convert RGB to Luminance
float rgb_to_luminance(vec3 rgb) {
	return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

// Generates a seed for a random number generator from 2 inputs plus a backoff
uint initRand(uint val0, uint val1, uint backoff)
{
	uint v0 = val0, v1 = val1, s0 = 0;

	for (uint n = 0; n < backoff; n++)
	{
		s0 += 0x9e3779b9;
		v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
		v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
	}
	return v0;
}

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float nextRand(inout uint s)
{
	s = (1664525u * s + 1013904223u);
	return float(s & 0x00FFFFFF) / float(0x01000000);
}

// Specular microfacet BRDF
vec3 specular_brdf(float roughness, vec3 V, vec3 L, vec3 N, vec3 H) {
	// Heaviside step functions
	float roughness_sq = roughness * roughness;
	float N_dot_H = dot(N, H);
	float N_dot_L = dot(N, L);
	float N_dot_V = dot(N, V);

	float f = (N_dot_H * N_dot_H * (roughness_sq - 1) + 1);

	float D_GGX = (roughness_sq * step(0, N_dot_H)) / (PI * (f * f));
	float V_GGX = 
		step(0, dot(H, L)) / (abs(N_dot_L) * sqrt(roughness_sq + (1 - roughness_sq) * N_dot_L * N_dot_L)) *
		step(0, dot(H, V)) / (abs(N_dot_V) * sqrt(roughness_sq + (1 - roughness_sq) * N_dot_V * N_dot_V));

	return vec3(V_GGX * D_GGX);
}

// Lambertian BRDF
vec3 diffuse_brdf(vec3 albedo) {
	return (1 / PI) * albedo;
}

float fresnel(vec3 V, vec3 H) {
	float V_dot_H = dot(V, H);
	float f = 1 - abs(V_dot_H);
	float f5 = f * f * f * f * f;

	return 0.04 + (1 - 0.04) * f;
}

vec3 material_brdf(vec3 albedo, float roughness, float metallic, vec3 V, vec3 L, vec3 N, vec3 H) {
	float V_dot_H = dot(V, H);
	float f = 1 - abs(V_dot_H);
	float f5 = f * f * f * f * f;
	vec3 specular = specular_brdf(roughness, V, L, N, H);
	vec3 dielectric_brdf = mix(diffuse_brdf(albedo), specular, fresnel(V, H));
	vec3 metal_brdf = specular * (albedo + (1 - albedo) * f5);
	return mix(dielectric_brdf, metal_brdf, metallic);
}

