#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(constant_id = SHADOW_MODE_SPEC_CONST_INDEX) const int shadow_mode = 0;
layout(constant_id = AMBIENT_OCCLUSION_MODE_SPEC_CONST_INDEX) const int ambient_occlusion_mode = 0;

layout(set = 3, binding = 0) uniform sampler2D position_texture;
layout(set = 3, binding = 1) uniform sampler2D normal_texture;
layout(set = 3, binding = 2) uniform sampler2D albedo_texture;
layout(set = 3, binding = 3) uniform sampler2D shadow_map;
layout(set = 3, binding = 4) uniform sampler2D raytraced_shadows_texture;
layout(set = 3, binding = 5) uniform sampler2D raytraced_ambient_occlusion_texture;
layout(set = 3, binding = 6) uniform sampler2D raytraced_reflections_texture;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

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

void main() {
	vec4 position_and_roughness = texture(position_texture, in_uv);
	vec3 packed_normal_and_metallic = texture(normal_texture, in_uv).xyw;
	vec3 position = position_and_roughness.xyz;
	vec3 camera_position = vec3(pfd.camera_view_inverse[3].xyz);
	vec3 albedo = texture(albedo_texture, in_uv).rgb;

	vec3 V = normalize(camera_position - position);
	vec3 L = -normalize(pfd.directional_light.direction.xyz);
	vec3 N = oct_decode_to_vec3(packed_normal_and_metallic.xy);
	vec3 H = normalize(L + V);
	vec3 light_color = pfd.directional_light.color.rgb;

	vec3 shadow = vec3(1.0);
	if(shadow_mode == SHADOW_MODE_RAYTRACED) {
		shadow = texture(raytraced_shadows_texture, in_uv).rgb; 
	}
	else if(shadow_mode == SHADOW_MODE_RASTERIZED) {
		vec4 pos_lightspace = SHADOW_BIAS_MATRIX * pfd.directional_light.projview * vec4(position, 1.0);
		vec4 shadow_coord = pos_lightspace / pos_lightspace.w;
		float depth = texture(shadow_map, shadow_coord.xy).r;
		shadow = shadow_coord.z > depth + 0.003 ? vec3(0.0) : vec3(1.0);
	}

	vec3 ao = vec3(1.0);
	if(ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_RAYTRACED) {
		ao = texture(raytraced_ambient_occlusion_texture, in_uv).rgb; 
	}

	float min_roughness = 0.04;
	float roughness = clamp(position_and_roughness.w, min_roughness, 1.0);
	float metallic = clamp(packed_normal_and_metallic.z, 0.0, 1.0);

	float V_dot_H = dot(V, H);
	float f = 1 - abs(V_dot_H);
	float f5 = f * f * f * f * f;
	vec3 specular = specular_brdf(roughness, V, L, N, H);
	vec3 dielectric_brdf = mix(diffuse_brdf(albedo), specular, fresnel(V, H));
	vec3 metal_brdf = specular * (albedo + (1 - albedo) * f5);
	vec3 material = mix(dielectric_brdf, metal_brdf, metallic);

	float ambient_factor = 0.3;
	vec3 diffuse_lighting = max(dot(N, L), 0.0) * material * light_color * shadow + texture(raytraced_reflections_texture, in_uv).rgb * shadow;
	out_color = vec4(diffuse_lighting, 1.0);
}

