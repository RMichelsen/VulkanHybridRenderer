#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common.glsl"
#include "../../../src/rendering_backend/glsl_common.h"

layout(push_constant) uniform PushConstants { HybridPushConstants pc; };

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv0;
layout(location = 4) in vec2 in_uv1;

layout(location = 0) out vec3 out_world_space_pos;
layout(location = 1) out vec3 out_clip_space_pos;
layout(location = 2) out vec3 out_normal;
layout(location = 3) out vec4 out_tangent;
layout(location = 4) out vec2 out_uv;
layout(location = 5) out vec4 out_reprojected_pos;

void main() {
	mat4 model = primitives[pc.object_id].transform;
	
	vec4 clip_space_pos = (pfd.camera_proj * pfd.camera_view * model) * vec4(in_pos, 1.0);
	out_world_space_pos = vec3(model * vec4(in_pos, 1.0));
	out_clip_space_pos = clip_space_pos.xyz;
	out_normal = in_normal;
	out_tangent = in_tangent;
	out_uv = in_uv0;

	out_reprojected_pos = (pfd.camera_proj_prev_frame * pfd.camera_view_prev_frame * model) * vec4(in_pos, 1.0);
	gl_Position = clip_space_pos;
}

