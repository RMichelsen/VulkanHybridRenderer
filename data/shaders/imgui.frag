#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 4) uniform sampler2D textures[];

layout(push_constant) uniform PushConstants {
	vec2 scale;
	vec2 translate;
	int texture_idx;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = in_color * texture(textures[pc.texture_idx], in_uv);
}