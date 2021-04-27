#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(push_constant) uniform PushConstants { DefaultPushConstants pc; };

layout(set = 3, binding = 0) uniform sampler2D shadow_map;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
	Primitive primitive = primitives[pc.object_id];

	vec4 albedo;
	if(primitive.material.base_color_texture == -1) {
		albedo = primitive.material.base_color;
	}
	else {
		albedo = texture(textures[primitive.material.base_color_texture], in_uv); 
	}
	if(primitive.material.alpha_mask == 1 && albedo.a < primitive.material.alpha_cutoff) {
		discard;
	}

	vec3 N = in_normal;
	if(primitives[pc.object_id].material.normal_map >= 0) {
		vec3 tangent_space_normal = normalize(texture(textures[primitive.material.normal_map], in_uv).xyz * 2.0 - 1.0);
		vec3 bitangent = cross(tangent_space_normal, in_tangent.xyz) * in_tangent.w;
		vec3 tangent = normalize(in_tangent.xyz - in_normal * dot(in_tangent.xyz, in_normal));
		N = tangent * tangent_space_normal.x + bitangent * tangent_space_normal.y + in_normal * tangent_space_normal.z;
	}

	vec3 light_dir = -pfd.directional_light.direction.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;
	vec3 ambient_light = 0.2 * albedo.rgb;

	vec4 pos_lightspace = SHADOW_BIAS_MATRIX * pfd.directional_light.projview * vec4(in_pos, 1.0);
	vec4 shadow_coord = pos_lightspace / pos_lightspace.w;
	float depth = texture(shadow_map, shadow_coord.xy).r;
	float shadow = shadow_coord.z < depth - 0.003 ? 0.0 : 1.0;
	vec3 diffuse_lighting = ambient_light + (max(dot(N, light_dir), 0.0) * albedo.rgb * light_color * shadow);
	out_color = vec4(diffuse_lighting, 1.0);
}

