#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(location = 0) rayPayloadInEXT vec4 payload;
layout(location = 1) rayPayloadEXT bool shadow_payload;
hitAttributeEXT vec3 hit_attribs;

void main() {
	Primitive primitive = primitives[gl_GeometryIndexEXT];

	ivec3 i = ivec3(indices[primitive.index_offset + 3 * gl_PrimitiveID + 0],
					indices[primitive.index_offset + 3 * gl_PrimitiveID + 1],
					indices[primitive.index_offset + 3 * gl_PrimitiveID + 2]);

	Vertex v0 = vertices[primitive.vertex_offset + i.x];
	Vertex v1 = vertices[primitive.vertex_offset + i.y];
	Vertex v2 = vertices[primitive.vertex_offset + i.z];

	const vec3 barycentrics = vec3(1.0 - hit_attribs.x - hit_attribs.y, hit_attribs.x, hit_attribs.y);
	vec2 uv = v0.uv0 * barycentrics.x + v1.uv0 * barycentrics.y + v2.uv0 * barycentrics.z;
	vec3 normal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
	vec3 position = vec3(primitive.transform * vec4(v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z, 1.0));

	vec3 albedo = texture(textures[primitive.material.base_color_texture], uv).rgb;

	vec3 N = normal;
	if(primitive.material.normal_map >= 0) {
		vec4 in_tangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z; 
		vec3 tangent_space_normal = normalize(texture(textures[primitive.material.normal_map], uv).xyz * 2.0 - 1.0);
		vec3 bitangent = cross(tangent_space_normal, in_tangent.xyz) * in_tangent.w;
		vec3 tangent = normalize(in_tangent.xyz - normal * dot(in_tangent.xyz, normal));
		N = tangent * tangent_space_normal.x + bitangent * tangent_space_normal.y + normal * tangent_space_normal.z;
	}

	vec3 light_dir = -pfd.directional_light.direction.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;
	vec3 albedo_lighting = 0.2 * albedo;

	shadow_payload = true;
	traceRayEXT(TLAS, gl_RayFlagsNoOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 
		0xFF, 0, 0, 1, position, 0.1, light_dir, 10000.0, 1);

	if(!shadow_payload) {
		payload = vec4(albedo_lighting + max(dot(N, light_dir), 0.0) * albedo * light_color, 1.0);
	}
	else {
		payload = vec4(albedo_lighting, 1.0);
	}
}

