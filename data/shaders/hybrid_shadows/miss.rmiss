#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec4 shadow_payload;

void main() {
	shadow_payload = vec4(1.0, 1.0, 1.0, 1.0);
}
