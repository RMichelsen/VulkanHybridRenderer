#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec4 payload;

void main() {
	if(gl_InstanceCustomIndexEXT == 0) {
		payload = vec4(1.0, 0.0, 0.0, 1.0);
	}
	else if(gl_InstanceCustomIndexEXT == 1) {
		payload = vec4(0.0, 1.0, 0.0, 1.0);
	}
	else if(gl_InstanceCustomIndexEXT == 2) {
		payload = vec4(0.0, 1.0, 0.0, 1.0);
	}
	else if(gl_InstanceCustomIndexEXT == 3) {
		payload = vec4(0.0, 1.0, 1.0, 1.0);
	}
	else if(gl_InstanceCustomIndexEXT == 4) {
		payload = vec4(1.0, 1.0, 0.0, 1.0);
	}
	else if(gl_InstanceCustomIndexEXT == 5) {
		payload = vec4(1.0, 1.0, 1.0, 1.0);
	}
	else {
		payload = vec4(0.0, 0.0, 1.0, 1.0);
	}
}