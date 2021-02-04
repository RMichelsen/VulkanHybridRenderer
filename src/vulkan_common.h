#pragma once

#define VK_CHECK(x) if((x) != VK_SUCCESS) { 			\
	assert(false); 										\
	printf("Vulkan error: %s:%i", __FILE__, __LINE__); 	\
}

inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

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

struct PerFrameData {
	glm::mat4 camera_view;
	glm::mat4 camera_proj;
};

struct PushConstants {
	int object_id;
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

struct MappedBuffer {
	VkBuffer handle;
	VmaAllocation allocation;
	void *mapped_data;
};

struct GPUBuffer {
	VkBuffer handle;
	VmaAllocation allocation;
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
	bool color_blending;
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

enum class VertexInputState {
	Default,
	Empty
};
enum class RasterizationState {
	Fill,
	Wireframe,
	FillCullCCW
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

struct PushConstantDescription {
	uint32_t size;
	VkPipelineStageFlags pipeline_stages;
};
inline constexpr PushConstantDescription PUSHCONSTANTS_NONE {
	.size = 0,
	.pipeline_stages = 0
};

struct GraphicsPipelineDescription {
	const char *name;
	const char *vertex_shader;
	const char *fragment_shader;
	VertexInputState vertex_input_state;
	RasterizationState rasterization_state;
	MultisampleState multisample_state;
	DepthStencilState depth_stencil_state;
	PushConstantDescription push_constants;
};

struct GraphicsPipeline {
	GraphicsPipelineDescription description;
	VkPipeline handle;
	VkPipelineLayout layout;
};

struct RaytracingPipelineDescription {

};

class GraphicsPipelineExecutionContext;
using GraphicsPipelineExecutionCallback = std::function<void(GraphicsPipelineExecutionContext &)>;
using ExecutePipelineCallback = std::function<void(std::string, GraphicsPipelineExecutionCallback)>;
using RenderPassCallback = std::function<void(ExecutePipelineCallback)>;

struct RenderPass {
	VkRenderPass handle;
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_set;
	std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers;
	std::vector<TransientResource> attachments;
	RenderPassCallback callback;
};

struct RenderPassDescription {
	std::vector<std::string> inputs;
	std::vector<std::string> outputs;
	std::vector<GraphicsPipelineDescription> pipeline_descriptions;
	RenderPassCallback callback;
};



