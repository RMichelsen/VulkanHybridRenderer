#version 460

layout(set = 1, binding = 0) uniform PerFrameData {
	mat4 camera_view;
	mat4 camera_proj;
} pfd;

layout(push_constant) uniform PushConstants {
	mat4 model;
	int texture;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv0;
layout(location = 3) in vec2 in_uv1;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_pos;
layout(location = 2) out vec3 out_normal;

void main() {
	out_uv = in_uv0;
	out_pos = (pc.model * vec4(in_pos, 1.0)).rgb;
	out_normal = in_normal;

	gl_Position = (pfd.camera_proj * pfd.camera_view * pc.model) * vec4(in_pos, 1.0);
}
