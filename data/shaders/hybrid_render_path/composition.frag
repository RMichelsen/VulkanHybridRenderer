#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(constant_id = SHADOW_MODE_SPEC_CONST_INDEX) const int shadow_mode = 0;
layout(constant_id = AMBIENT_OCCLUSION_MODE_SPEC_CONST_INDEX) const int ambient_occlusion_mode = 0;
layout(constant_id = REFLECTION_MODE_SPEC_CONST_INDEX) const int reflection_mode = 0;

layout(set = 3, binding = 0) uniform sampler2D albedo;
layout(set = 3, binding = 1) uniform sampler2D world_space_normals_and_object_ids;
layout(set = 3, binding = 2) uniform sampler2D motion_vectors_and_metallic_roughness;
layout(set = 3, binding = 3) uniform sampler2D depth;
layout(set = 3, binding = 4) uniform sampler2D shadow_map;
layout(set = 3, binding = 5) uniform sampler2D screen_space_ambient_occlusion;
layout(set = 3, binding = 6) uniform sampler2D screen_space_reflections;
layout(set = 3, binding = 7) uniform sampler2D raytraced_shadow_and_ao_texture;
layout(set = 3, binding = 8) uniform sampler2D raytraced_reflections_texture;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
	vec3 albedo = texture(albedo, in_uv).rgb;
	float depth = texture(depth, in_uv).x;
	vec3 P = get_world_space_position(depth, in_uv);
	vec3 N = texture(world_space_normals_and_object_ids, in_uv).xyz;
	vec2 metallic_roughness = texture(motion_vectors_and_metallic_roughness, in_uv).zw;

	vec2 raytraced_shadow_and_ao = vec2(1.0, 1.0);
	if(shadow_mode == SHADOW_MODE_RAYTRACED || ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_RAYTRACED) {
		raytraced_shadow_and_ao = texture(raytraced_shadow_and_ao_texture, in_uv).xy;
	}

	vec3 camera_position = vec3(pfd.camera_view_inverse[3].xyz);
	vec3 V = normalize(camera_position - P);
	vec3 L = -pfd.directional_light.direction.xyz;
	vec3 H = normalize(L + V);

	float shadow = 1.0;
	if(shadow_mode == SHADOW_MODE_RAYTRACED) {
		shadow = raytraced_shadow_and_ao.x;
	}
	else if(shadow_mode == SHADOW_MODE_RASTERIZED) {
		vec4 pos_lightspace = SHADOW_BIAS_MATRIX * pfd.directional_light.projview * vec4(P, 1.0);
		vec4 shadow_coord = pos_lightspace / pos_lightspace.w;
		float depth = texture(shadow_map, shadow_coord.xy).r;
		shadow = shadow_coord.z < depth - 0.003 ? 0.0 : 1.0;
	}

	float ao = 1.0;
	if(ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_RAYTRACED) {
		ao = raytraced_shadow_and_ao.y;
	}
	else if(ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_SSAO) {
		ao = texture(screen_space_ambient_occlusion, in_uv).x;
	}

	float min_roughness = 0.04;
	float metallic = clamp(metallic_roughness.x, 0.0, 1.0);
	float roughness = clamp(metallic_roughness.y, min_roughness, 1.0);

	float ambient_factor = PI_INVERSE;
	vec3 light_intensity = pfd.directional_light.intensity.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;

	vec3 f0 = vec3(0.04);
	f0 = mix(f0, albedo, metallic);
	vec3 F = fresnel_schlick(f0, H, V);

	float N_dot_L = max(dot(N, L), 0.0);

	vec3 ambient_lighting = ao * albedo * ambient_factor;
	vec3 diffuse_lighting = diffuse_brdf(metallic, albedo, F) * N_dot_L * light_intensity * light_color * shadow;
	vec3 specular_lighting = specular_brdf(roughness, F, V, L, N, H) * N_dot_L * light_intensity * light_color * shadow;

	if(reflection_mode == REFLECTION_MODE_RAYTRACED) {
		vec3 reflections = texture(raytraced_reflections_texture, in_uv).rgb * shadow;
		if(metallic == 1.0) {
			specular_lighting = reflections;
		}
		else {
			specular_lighting = mix(specular_lighting, reflections, roughness);
		}
	}
	else if(reflection_mode == REFLECTION_MODE_SSR) {
		vec3 reflections = texture(screen_space_reflections, in_uv).rgb * shadow;
		if(metallic == 1.0) {
			specular_lighting = reflections;
		}
		else {
			specular_lighting = mix(specular_lighting, reflections, roughness);
		}
	}

	vec3 lighting = ambient_lighting + diffuse_lighting + specular_lighting;
	
	out_color = vec4(lighting, 1.0);
}
