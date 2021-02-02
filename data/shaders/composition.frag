#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 2, binding = 0) uniform sampler2D position;
layout(set = 2, binding = 1) uniform sampler2D normal;
layout(set = 2, binding = 2) uniform sampler2D albedo;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
	if(in_uv.x + in_uv.y < 0.66) out_color = texture(position, in_uv);
	else if(in_uv.x + in_uv.y < 1.32) out_color = texture(normal, in_uv);
	else if(in_uv.x + in_uv.y < 2.0) out_color = texture(albedo, in_uv);
}