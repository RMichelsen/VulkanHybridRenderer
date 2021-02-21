#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "common.glsl"

layout(set = 0, binding = 0, scalar) buffer Vertices { Vertex vertices[]; };
layout(set = 0, binding = 1) buffer Indices { uint indices[]; };
layout(set = 0, binding = 2, scalar) buffer Primitives { Primitive primitives[]; };
layout(set = 0, binding = 3) uniform accelerationStructureEXT TLAS;
layout(set = 0, binding = 4) uniform sampler2D textures[];

layout(set = 1, binding = 0) uniform PerFrameData {
	mat4 camera_view;
	mat4 camera_proj;
	mat4 camera_view_inverse;
	mat4 camera_proj_inverse;
	DirectionalLight directional_light;
	float split_view_anchor;
} pfd;

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

	vec3 albedo = texture(textures[primitive.texture_idx], uv).rgb;

	vec3 light_dir = -pfd.directional_light.direction.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;
	vec3 albedo_lighting = 0.4 * albedo;

	shadow_payload = true;
	traceRayEXT(TLAS, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 
		0xFF, 0, 0, 1, position, 0.0001, light_dir, 10000.0, 1);

	if(!shadow_payload) {
		payload = vec4(albedo_lighting + max(dot(normal, light_dir), 0.0) * albedo * light_color, 1.0);
	}
	else {
		payload = vec4(albedo_lighting, 1.0);
	}
}

