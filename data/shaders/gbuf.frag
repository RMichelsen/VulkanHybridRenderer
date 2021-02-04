#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"

layout(set = 0, binding = 2, std430) buffer Primitives { Primitive primitives[]; };
layout(set = 0, binding = 3) uniform sampler2D textures[];

layout(push_constant) uniform PushConstants {
	int object_id;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec3 in_pos;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec4 out_pos;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_albedo;

void main() {
	out_pos = vec4(in_pos, 1.0);
	out_normal = vec4(in_normal, 1.0);
	out_albedo = texture(textures[primitives[pc.object_id].texture], in_uv);
}