#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(push_constant) uniform PushConstants { DefaultPushConstants pc; };

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
	vec3 albedo = texture(textures[primitives[pc.object_id].material.base_color_texture], in_uv).rgb;

	vec3 N = in_normal;
	if(primitives[pc.object_id].material.normal_map >= 0) {
		vec3 tangent_space_normal = normalize(texture(textures[primitives[pc.object_id].material.normal_map], in_uv).xyz * 2.0 - 1.0);
		vec3 bitangent = cross(tangent_space_normal, in_tangent.xyz) * in_tangent.w;
		vec3 tangent = normalize(in_tangent.xyz - in_normal * dot(in_tangent.xyz, in_normal));
		N = tangent * tangent_space_normal.x + bitangent * tangent_space_normal.y + in_normal * tangent_space_normal.z;
	}

	vec3 light_dir = -pfd.directional_light.direction.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;

	rayQueryEXT ray_query;
	rayQueryInitializeEXT(ray_query, TLAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, in_pos, 0.1, light_dir, 10000.0);

	while(rayQueryProceedEXT(ray_query)) {}

	vec3 in_shadow = vec3(1.0);
	if(rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
		in_shadow = vec3(0.0);
	}

	vec3 ambient_light = 0.2 * albedo;
	vec3 diffuse_lighting = ambient_light + (max(dot(N, light_dir), 0.0) * albedo * light_color * in_shadow);
	out_color = vec4(diffuse_lighting, 1.0);
}

