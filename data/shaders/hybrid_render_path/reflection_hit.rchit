#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(location = 0) rayPayloadEXT vec4 shadow_payload;
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
	float metallic = primitive.material.metallic_factor;
	float roughness = primitive.material.roughness_factor;
	if(primitive.material.metallic_roughness_texture != -1) {
		vec4 metallic_roughness = texture(textures[primitive.material.metallic_roughness_texture], uv);
		metallic *= metallic_roughness.g;
		roughness *= metallic_roughness.b;
	}

	vec3 camera_position = pfd.camera_view_inverse[3].xyz;
	vec3 V = normalize(camera_position - position);
	vec3 L = -pfd.directional_light.direction.xyz;
	vec3 N = normal;
	vec3 H = normalize(L + V);
	vec3 ray_launch_position = position + N * 0.01;

	// Trace shadow ray
	shadow_payload = vec4(0.0, 0.0, 0.0, 0.0);
	traceRayEXT(TLAS, gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsTerminateOnFirstHitEXT, 
		0xFF, 0, 0, 0, ray_launch_position, 0.01, L, 10000.0, 0);

	float min_roughness = 0.04;
	roughness = clamp(roughness, min_roughness, 1.0);
	metallic = clamp(metallic, 0.0, 1.0);

	// Assume quite low ambient contribution, 
	// in reality would need to trace AO rays, but too expensive
	float ambient_factor = PI_INVERSE * 0.2;
	vec3 light_intensity = pfd.directional_light.intensity.xyz;
	vec3 light_color = pfd.directional_light.color.rgb;

	vec3 f0 = vec3(0.04);
	f0 = mix(f0, albedo, metallic);
	vec3 F = fresnel_schlick(f0, H, V);

	vec3 ambient_lighting = albedo * ambient_factor;
	vec3 diffuse_lighting = diffuse_brdf(metallic, albedo, F);
	vec3 specular_lighting = specular_brdf(roughness, F, V, L, N, H);
	vec3 lighting = ambient_lighting + (diffuse_lighting + specular_lighting) * max(dot(N, L), 0.0) * light_intensity * light_color * shadow_payload.xyz;
	reflection_payload = vec4(lighting, 1.0);
}

