#pragma once

#define VK_CHECK(x) if((x) != VK_SUCCESS) { 			\
	assert(false); 										\
	printf("Vulkan error: %s:%i", __FILE__, __LINE__); 	\
}

inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

struct Image {
	VkImage handle;
	VkImageView view;
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
	glm::mat4 camera_view_inverse;
	glm::mat4 camera_proj_inverse;
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

struct AccelerationStructure {
	VkAccelerationStructureKHR handle;
	GPUBuffer buffer;
	GPUBuffer scratch;
};

enum class TransientResourceType {
	AttachmentImage,
	SampledImage,
	StorageImage,
	Buffer
};

struct TransientAttachmentImage {
	VkFormat format;
	uint32_t width;
	uint32_t height;
	bool color_blending;
};

struct TransientSampledImage {
	VkFormat format;
	uint32_t width;
	uint32_t height;
};

struct TransientStorageImage {
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
		TransientAttachmentImage attachment_image;
		TransientSampledImage sampled_image;
		TransientStorageImage storage_image;
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
	const char *name;
	const char *raygen_shader;
	const char *hit_shader;
	const char *miss_shader;
};

struct RaytracingPipeline {
	RaytracingPipelineDescription description;
	std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> shader_groups;
	uint32_t shader_group_size;
	MappedBuffer shader_binding_table;
	VkDeviceAddress shader_binding_table_address;
	VkPipeline handle;
	VkPipelineLayout layout;
};

class GraphicsPipelineExecutionContext;
using GraphicsPipelineExecutionCallback = std::function<void(GraphicsPipelineExecutionContext &)>;
using ExecuteGraphicsPipelineCallback = std::function<void(std::string, GraphicsPipelineExecutionCallback)>;
using GraphicsPassCallback = std::function<void(ExecuteGraphicsPipelineCallback)>;

class RaytracingPipelineExecutionContext;
using RaytracingPipelineExecutionCallback = std::function<void(RaytracingPipelineExecutionContext &)>;
using ExecuteRaytracingPipelineCallback = std::function<void(std::string, RaytracingPipelineExecutionCallback)>;
using RaytracingPassCallback = std::function<void(ExecuteRaytracingPipelineCallback)>;

struct GraphicsPass {
	VkRenderPass handle;
	std::vector<TransientResource> attachments;
	std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers;
	GraphicsPassCallback callback;
};

struct ImageAccess {
	VkImageLayout layout;
	VkFormat format;
	VkAccessFlags access_flags;
	VkPipelineStageFlags stage_flags;
};

struct ImageLayoutTransition {
	std::string image_name;
	VkFormat format;
	VkImageLayout src_layout;
	VkImageLayout dst_layout;
	VkAccessFlags src_access;
	VkAccessFlags dst_access;
	VkPipelineStageFlags src_stage;
	VkPipelineStageFlags dst_stage;
};

struct RaytracingPass {
	RaytracingPassCallback callback;
};

struct RenderPass {
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_set;
	std::vector<ImageLayoutTransition> preparation_transitions;
	std::variant<GraphicsPass, RaytracingPass> pass;
};

struct GraphicsPassDescription {
	std::vector<GraphicsPipelineDescription> pipeline_descriptions;
	GraphicsPassCallback callback;
};

struct RaytracingPassDescription {
	RaytracingPipelineDescription pipeline_description;
	RaytracingPassCallback callback;
};

struct RenderPassDescription {
	std::vector<TransientResource> inputs;
	std::vector<TransientResource> outputs;

	std::variant<GraphicsPassDescription, RaytracingPassDescription> description;
};



