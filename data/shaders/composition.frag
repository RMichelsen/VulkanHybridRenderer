#version 460
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(set = 1, binding = 0) uniform PerFrameData {
	mat4 camera_view;
	mat4 camera_proj;
	mat4 camera_view_inverse;
	mat4 camera_proj_inverse;
	DirectionalLight directional_light;
	float anchor;
} pfd;

layout(set = 2, binding = 0) uniform sampler2D position_texture;
layout(set = 2, binding = 1) uniform sampler2D normal_texture;
layout(set = 2, binding = 2) uniform sampler2D albedo_texture;
layout(set = 2, binding = 3) uniform sampler2D rays_texture;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
	if(in_uv.x < (pfd.anchor - 0.001f)) {
		//	vec3 position = texture(position_texture, in_uv).rgb;
		vec3 normal = texture(normal_texture, in_uv).rgb;
		vec3 albedo = texture(albedo_texture, in_uv).rgb;

		vec3 light_dir = -pfd.directional_light.direction.xyz;
		vec3 light_color = pfd.directional_light.color.rgb;

		vec3 diffuse_lighting = 0.4f * albedo + max(dot(normal, light_dir), 0.0f) * albedo * light_color;
		out_color = vec4(diffuse_lighting, 1.0f);
	}
	else if(in_uv.x < (pfd.anchor + 0.001f)) {
		out_color = vec4(1.0, 0.0, 0.0, 1.0);
	}
	else {
		out_color = texture(rays_texture, in_uv);
	}

//	if(in_uv.x + in_uv.y < 0.998) out_color = texture(albedo, in_uv);
//	else if(in_uv.x + in_uv.y < 1.002) out_color = vec4(1.0, 0.0, 0.0, 1.0);
//	else out_color = texture(output_image, in_uv);
}

