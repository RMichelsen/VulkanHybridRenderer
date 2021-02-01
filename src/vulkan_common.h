#pragma once

#define VK_CHECK(x) if((x) != VK_SUCCESS) { 			\
	assert(false); 										\
	printf("Vulkan error: %s:%i", __FILE__, __LINE__); 	\
}

#define MAX_FRAMES_IN_FLIGHT 3

struct Texture {
	VkImage image;
	VkImageView image_view;
	VmaAllocation allocation;
};

struct Camera {
	glm::mat4 perspective;
	glm::mat4 view;
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
	Camera camera;
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

struct GraphicsPipeline {
	VkPipeline handle;
	VkPipelineLayout layout;
};

struct RaytracingPipeline {
	VkPipeline handle;
	VkPipelineLayout layout;
};

enum class TransientResourceType {
	Texture,
	Buffer
};

struct TransientTexture {
	VkFormat format;
	uint32_t width;
	uint32_t height;
};

struct TransientBuffer {
	uint32_t stride;
	uint32_t count;
};

struct TransientResource {
	const char *name;
	TransientResourceType type;
	union {
		TransientTexture texture;
		TransientBuffer buffer;
	};
};

inline constexpr TransientResource TRANSIENT_BACKBUFFER {
	.name = "BACKBUFFER"
};

enum class RasterizationState {
	Fill,
	Wireframe
};
enum class MultisampleState {
	Off
};
enum class DepthStencilState {
	On,
	Off
};
enum class ColorBlendState {
	Off
};

struct GraphicsPipelineDescription {
	const char *name;
	const char *vertex_shader;
	const char *fragment_shader;
	RasterizationState rasterization_state;
	MultisampleState multisample_state;
	DepthStencilState depth_stencil_state;
	std::vector<ColorBlendState> color_blend_states;
};

struct RaytracingPipelineDescription {

};
struct RenderPass {
	VkRenderPass handle;
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_set;
	std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers;
	std::vector<std::string> attachments;
	std::function<void(RenderPass &render_pass, std::unordered_map<std::string, GraphicsPipeline> &pipelines, VkCommandBuffer &command_buffer)> executor;
};

struct RenderPassDescription {
	std::vector<std::string> inputs;
	std::vector<std::string> outputs;
	std::vector<GraphicsPipelineDescription> graphics_pipelines;
	std::function<void(RenderPass &render_pass, std::unordered_map<std::string, GraphicsPipeline> &pipelines, VkCommandBuffer &command_buffer)> executor;
};

