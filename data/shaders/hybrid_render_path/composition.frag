#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(constant_id = SHADOW_MODE_SPEC_CONST_INDEX) const int shadow_mode = 0;

layout(set = 3, binding = 0) uniform sampler2D position_texture;
layout(set = 3, binding = 1) uniform sampler2D normal_texture;
layout(set = 3, binding = 2) uniform sampler2D albedo_texture;
layout(set = 3, binding = 3) uniform sampler2D shadow_map;
layout(set = 3, binding = 4) uniform sampler2D raytraced_shadows_texture;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
	vec3 normal = texture(normal_texture, in_uv).rgb;
	vec3 albedo = texture(albedo_texture, in_uv).rgb;

	// Get light direction in view space
	vec3 light_dir = -normalize(vec3(pfd.camera_view * vec4(pfd.directional_light.direction.xyz, 0.0)));
	
	vec3 light_color = pfd.directional_light.color.rgb;
	vec3 ambient_light = 0.2 * albedo;

	if(shadow_mode == SHADOW_MODE_RAYTRACED) {
		vec3 shadow_payload = texture(raytraced_shadows_texture, in_uv).rgb; 
		vec3 diffuse_lighting = ambient_light + (max(dot(normal, light_dir), 0.0) * albedo * light_color * shadow_payload);
		out_color = vec4(diffuse_lighting, 1.0);
	}
	else if(shadow_mode == SHADOW_MODE_RASTERIZED) {
		vec3 pos = texture(position_texture, in_uv).rgb;
		vec4 pos_lightspace = SHADOW_BIAS_MATRIX * pfd.directional_light.projview * vec4(pos, 1.0);
		vec4 shadow_coord = pos_lightspace / pos_lightspace.w;
		float depth = texture(shadow_map, shadow_coord.xy).r;
		float shadow = shadow_coord.z > depth + 0.003 ? 0.0 : 1.0;
		vec3 diffuse_lighting = ambient_light + (max(dot(normal, light_dir), 0.0) * albedo * light_color * shadow);
		out_color = vec4(diffuse_lighting, 1.0);
	}
	else {
		out_color = vec4(ambient_light, 1.0);
	}
}

