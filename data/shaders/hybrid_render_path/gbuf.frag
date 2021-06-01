#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(push_constant) uniform PushConstants { HybridPushConstants pc; };

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec4 in_tangent;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_reprojected_pos;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_world_space_normals_and_object_ids;
layout(location = 2) out vec4 out_motion_vectors_and_metallic_roughness;

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
	if(albedo.a == 0.0f) {
		discard;
	}
	out_albedo = albedo;

	vec3 N = in_normal;
	if(primitive.material.normal_map >= 0) {
		vec3 tangent_space_normal = normalize(texture(textures[primitive.material.normal_map], in_uv).xyz * 2.0 - 1.0);
		vec3 bitangent = cross(tangent_space_normal, in_tangent.xyz) * in_tangent.w;
		vec3 tangent = normalize(in_tangent.xyz - in_normal * dot(in_tangent.xyz, in_normal));
		N = tangent * tangent_space_normal.x + bitangent * tangent_space_normal.y + in_normal * tangent_space_normal.z;
	}

	out_world_space_normals_and_object_ids = vec4(normalize(mat3(pc.normal_matrix) * N), pc.object_id);

	// Motion vector
	vec2 current_ndc_pos = gl_FragCoord.xy * pfd.display_size_inverse;
	vec2 prev_ndc_pos = (in_reprojected_pos.xy / in_reprojected_pos.w) * 0.5 + 0.5;

	// Metallic and roughness material parameters
	float metallic = primitive.material.metallic_factor;
	float roughness = primitive.material.roughness_factor;
	if(primitive.material.metallic_roughness_texture != -1) {
		vec4 metallic_roughness = texture(textures[primitive.material.metallic_roughness_texture], in_uv);
		metallic *= metallic_roughness.g;
		roughness *= metallic_roughness.b;
	}

	out_motion_vectors_and_metallic_roughness = vec4(current_ndc_pos - prev_ndc_pos, metallic, roughness);
}
