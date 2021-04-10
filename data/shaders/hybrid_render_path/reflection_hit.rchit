#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(location = 1) rayPayloadInEXT vec4 reflection_payload;
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

	vec3 albedo;
	if(primitive.material.base_color_texture == -1) {
		albedo = primitive.material.base_color.rgb;
	}
	else {
		albedo = texture(textures[primitive.material.base_color_texture], uv).rgb;
	}

	reflection_payload = vec4(albedo, 1.0);
}

