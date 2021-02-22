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
	float split_view_anchor;
} pfd;

layout(push_constant) uniform PushConstants {
	mat4 light_projview;
	int object_id;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv0;
layout(location = 3) in vec2 in_uv1;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_pos;
layout(location = 2) out vec4 out_pos_lightspace;
layout(location = 3) out vec3 out_normal;

const mat4 bias_matrix = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0
);

void main() {
	mat4 model = primitives[pc.object_id].transform;

	vec3 pos = vec3(model * vec4(in_pos, 1.0));
	out_uv = in_uv0;
	out_pos = pos;
	out_pos_lightspace = bias_matrix * pc.light_projview * vec4(pos, 1.0);
	out_normal = in_normal;

	gl_Position = (pfd.camera_proj * pfd.camera_view * model) * vec4(in_pos, 1.0);
}

