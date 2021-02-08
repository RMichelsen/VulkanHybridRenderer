#version 460

layout(set = 2, binding = 0) uniform sampler2D position;
layout(set = 2, binding = 1) uniform sampler2D normal;
layout(set = 2, binding = 2) uniform sampler2D albedo;
layout(set = 2, binding = 3) uniform sampler2D output_image;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
	if(in_uv.x + in_uv.y < 0.998) out_color = texture(albedo, in_uv);
	else if(in_uv.x + in_uv.y < 1.002) out_color = vec4(1.0, 0.0, 0.0, 1.0);
	else out_color = texture(output_image, in_uv);
}