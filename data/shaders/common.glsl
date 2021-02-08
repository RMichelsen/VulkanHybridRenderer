struct Vertex {
	vec3 pos;
	vec3 normal;
	vec2 uv0;
	vec2 uv1;
};

struct Primitive {
	mat4 transform;
	int vertex_offset;
	int index_offset;
	int index_count;
	int texture;
};
