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

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
	vec3 normal = texture(normal_texture, in_uv).rgb;
	vec3 albedo = texture(albedo_texture, in_uv).rgb;
	float ambient_factor = 0.3;

	// Get light direction in view space
	vec3 light_dir = -pfd.directional_light.direction.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;

	vec3 shadow = vec3(1.0);
	if(shadow_mode == SHADOW_MODE_RAYTRACED) {
		shadow = texture(raytraced_shadows_texture, in_uv).rgb; 
	}
	else if(shadow_mode == SHADOW_MODE_RASTERIZED) {
		vec3 pos = texture(position_texture, in_uv).rgb;
		vec4 pos_lightspace = SHADOW_BIAS_MATRIX * pfd.directional_light.projview * vec4(pos, 1.0);
		vec4 shadow_coord = pos_lightspace / pos_lightspace.w;
		float depth = texture(shadow_map, shadow_coord.xy).r;
		shadow = shadow_coord.z > depth + 0.003 ? vec3(0.0) : vec3(1.0);
	}

	vec3 ao = vec3(1.0);
	if(ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_RAYTRACED) {
		ao = texture(raytraced_ambient_occlusion_texture, in_uv).rgb; 
	}

	vec3 diffuse_lighting = albedo * ambient_factor * ao + (max(dot(normal, light_dir), 0.0) * albedo * light_color * shadow);
	out_color = vec4(diffuse_lighting, 1.0);
}

