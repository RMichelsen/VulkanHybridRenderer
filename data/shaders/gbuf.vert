#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"

layout(set = 0, binding = 2, std430) buffer Primitives { Primitive primitives[]; };
layout(set = 1, binding = 0) uniform PerFrameData {
	mat4 camera_view;
	mat4 camera_proj;
	mat4 camera_view_inverse;
	mat4 camera_proj_inverse;
	DirectionalLight directional_light;
	float anchor;
} pfd;

layout(push_constant) uniform PushConstants {
	int object_id;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv0;
layout(location = 3) in vec2 in_uv1;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_pos;
layout(location = 2) out vec3 out_normal;

void main() {
	mat4 model = primitives[pc.object_id].transform;

	out_uv = in_uv0;
	out_pos = vec3(model * vec4(in_pos, 1.0));
	out_normal = in_normal;

	gl_Position = (pfd.camera_proj * pfd.camera_view * model) * vec4(in_pos, 1.0);
}

