#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec4 payload;

void main() {
	payload = vec4(0.3, 0.8, 0.2, 1.0);
}
