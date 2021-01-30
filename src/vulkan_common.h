#pragma once

#define VK_CHECK(x) if((x) != VK_SUCCESS) { 			\
	assert(false); 										\
	printf("Vulkan error: %s:%i", __FILE__, __LINE__); 	\
}

struct Texture {
	VkImage image;
	VkImageView image_view;
	VmaAllocation allocation;
};

struct Primitive {
	glm::mat4 transform;
	uint32_t vertex_offset;
	uint32_t index_offset;
	uint32_t index_count;
	int texture;
};

struct Mesh {
	std::vector<Primitive> primitives;
};

struct Scene {
	std::vector<Mesh> meshes;
};

struct PushConstants {
	glm::mat4 MVP;
	int texture;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv0;
	glm::vec2 uv1;

};

constexpr VkVertexInputBindingDescription VERTEX_BINDING_DESCRIPTION {
	.binding = 0,
	.stride = sizeof(Vertex),
	.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
};
constexpr std::array<VkVertexInputAttributeDescription, 4> VERTEX_ATTRIBUTE_DESCRIPTIONS {
	VkVertexInputAttributeDescription {
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(Vertex, pos)
	},
	VkVertexInputAttributeDescription {
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(Vertex, normal)
	},
	VkVertexInputAttributeDescription {
		.location = 2,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(Vertex, uv0)
	},
	VkVertexInputAttributeDescription {
		.location = 3,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(Vertex, uv1)
	}
};

template<typename T>
struct MappedBuffer {
	VkBuffer handle;
	VmaAllocation allocation;
	T *mapped_data;
	uint32_t offset = 0;

	void Insert(T v) {	
		*(mapped_data + offset) = v;
		++offset;
	}
};
using VertexBuffer = MappedBuffer<Vertex>;
using IndexBuffer = MappedBuffer<uint32_t>;


