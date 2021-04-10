#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 1) rayPayloadInEXT vec4 reflection_payload;

void main() {
	reflection_payload = vec4(0.0, 0.0, 0.0, 0.0);
}
