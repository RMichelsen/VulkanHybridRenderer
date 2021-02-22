#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable

#include "common.glsl"

layout(set = 0, binding = 2, scalar) buffer Primitives { Primitive primitives[]; };
layout(set = 0, binding = 4) uniform sampler2D textures[];
layout(set = 1, binding = 0) uniform PerFrameData {
	mat4 camera_view;
	mat4 camera_proj;
	mat4 camera_view_inverse;
	mat4 camera_proj_inverse;
	DirectionalLight directional_light;
	float split_view_anchor;
} pfd;

layout(set = 2, binding = 0) uniform sampler2D shadow_map; 

layout(push_constant) uniform PushConstants {
	mat4 light_projview;
	int object_id;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec3 in_pos;
layout(location = 2) in vec4 in_pos_lightspace;
layout(location = 3) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

float shadow() {
	vec4 shadow_coord = in_pos_lightspace / in_pos_lightspace.w;
	float depth = texture(shadow_map, shadow_coord.xy).r;
	
	if(shadow_coord.z > depth + 0.003) {
		return 0.0;
	}

	return 1.0;
}

void main() {
	vec3 albedo = texture(textures[primitives[pc.object_id].texture_idx], in_uv).rgb;

	vec3 light_dir = -pfd.directional_light.direction.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;

	float in_shadow = shadow();
	vec3 ambient_light = 0.4 * albedo;
	vec3 diffuse_lighting = ambient_light + (max(dot(in_normal, light_dir), 0.0) * albedo * light_color * in_shadow);
	out_color = vec4(diffuse_lighting, 1.0);

}

