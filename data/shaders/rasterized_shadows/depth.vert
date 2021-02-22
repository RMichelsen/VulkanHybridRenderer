#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"

layout(set = 0, binding = 2, std430) buffer Primitives { Primitive primitives[]; };
layout(push_constant) uniform PushConstants {
	mat4 light_projview;
	int object_id;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv0;
layout(location = 3) in vec2 in_uv1;

void main() {
	mat4 model = primitives[pc.object_id].transform;
	vec3 pos = vec3(model * vec4(in_pos, 1.0));
	gl_Position = pc.light_projview * vec4(pos, 1.0);
}

