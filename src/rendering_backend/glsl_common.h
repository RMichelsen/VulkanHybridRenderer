#ifdef __cplusplus
#pragma once
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"
using mat4 = glm::mat4;
using ivec2 = glm::ivec2;
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using uint = uint32_t;
#endif

#ifndef __cplusplus
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#endif

struct DefaultPushConstants {
	int object_id;
};

struct HybridPushConstants {
	mat4 normal_matrix;
	int object_id;
};

struct SVGFPushConstants {
	// Pingpong texture
	ivec2 integrated_shadow_and_ao;

	int prev_frame_normals_and_object_ids;
	int shadow_and_ao_history;
	int shadow_and_ao_moments_history;
	int atrous_step;
};

struct SSRPushConstants {
	float ray_distance;
	float step_size;
	float thickness;
	int bsearch_steps;
};

struct SSAOPushConstants {
	float radius;
};

struct DirectionalLight {
	mat4 projview;
	vec4 direction;
	vec4 color;
	vec4 intensity;
};

struct PerFrameData {
	mat4 camera_view;
	mat4 camera_proj;
	mat4 camera_view_inverse;
	mat4 camera_proj_inverse;
	mat4 camera_viewproj_inverse;
	mat4 camera_view_prev_frame;
	mat4 camera_proj_prev_frame;
	DirectionalLight directional_light;
	vec2 display_size;
	vec2 display_size_inverse;
	uint frame_index;
	int blue_noise_texture_index;
};

struct Vertex {
	vec3 pos;
	vec3 normal;
	vec4 tangent;
	vec2 uv0;
	vec2 uv1;
};

struct Material {
	vec4 base_color;
	int base_color_texture;
	int metallic_roughness_texture;
	int normal_map;
	float metallic_factor;
	float roughness_factor;
	int alpha_mask;
	float alpha_cutoff;
};

struct Primitive {
	mat4 transform;
	Material material;
	uint vertex_offset;
	uint index_offset;
	uint index_count;
};

#ifndef __cplusplus
layout(set = 0, binding = 0, scalar) buffer Vertices { Vertex vertices[]; };
layout(set = 0, binding = 1, scalar) buffer Indices { uint indices[]; };
layout(set = 0, binding = 2, scalar) buffer Primitives { Primitive primitives[]; };
layout(set = 0, binding = 3) uniform accelerationStructureEXT TLAS;
layout(set = 0, binding = 4) uniform sampler2D textures[];
layout(set = 1, binding = 0) uniform image2D storage_images[];
layout(set = 2, binding = 0) uniform PFD { PerFrameData pfd; };

// Reconstruct view-space position from depth
vec3 get_view_space_position(float depth, vec2 uv) {
	vec4 reprojected_position = pfd.camera_proj_inverse * vec4(uv * 2.0 - vec2(1.0), depth, 1.0);
	vec3 view_space_position = reprojected_position.xyz / reprojected_position.w;
	return view_space_position;
}

// Reconstruct world-space position from depth
vec3 get_world_space_position(float depth, vec2 uv) {
	vec4 reprojected_position = pfd.camera_viewproj_inverse * vec4(uv * 2.0 - vec2(1.0), depth, 1.0);
	vec3 world_space_position = reprojected_position.xyz / reprojected_position.w;
	return world_space_position;
}
#endif

