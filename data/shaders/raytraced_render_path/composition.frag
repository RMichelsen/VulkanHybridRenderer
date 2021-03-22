#version 460
#extension GL_GOOGLE_include_directive : require

#include "../common.glsl"

layout(set = 3, binding = 0) uniform sampler2D raytraced_output;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
	out_color = texture(raytraced_output, in_uv);
}

