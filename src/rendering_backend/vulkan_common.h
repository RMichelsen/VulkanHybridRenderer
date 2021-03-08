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
	uint32_t width;
	uint32_t height;
	VkFormat format;
	VkImageUsageFlags usage;
};

struct SamplerInfo {
	VkFilter mag_filter;
	VkFilter min_filter;
	VkSamplerAddressMode address_mode_u;
	VkSamplerAddressMode address_mode_v;
};

struct Sampler {
	VkSampler handle;
	SamplerInfo info;
};

struct Camera {
	glm::mat4 perspective;
	glm::mat4 transform;
	glm::mat4 view;

	float yaw;
	float pitch;
	float roll;
};

struct Material {
	int base_color_texture;
	int normal_map;
	int alpha_mask;
	float alpha_cutoff;
};

struct Primitive {
	glm::mat4 transform;
	Material material;
	uint32_t vertex_offset;
	uint32_t index_offset;
	uint32_t index_count;
};

struct Mesh {
	std::vector<Primitive> primitives;
};

struct DirectionalLight {
	glm::mat4 projview;
	glm::vec4 direction;
	glm::vec4 color;
};

struct Scene {
	Camera camera;
	DirectionalLight directional_light;
	std::vector<Mesh> meshes;
};

struct PerFrameData {
	glm::mat4 camera_view;
	glm::mat4 camera_proj;
	glm::mat4 camera_view_inverse;
	glm::mat4 camera_proj_inverse;
	DirectionalLight directional_light;
};

struct PushConstants {
	int object_id;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec4 tangent;
	glm::vec2 uv0;
	glm::vec2 uv1;
};
constexpr VkVertexInputBindingDescription DEFAULT_VERTEX_BINDING_DESCRIPTION {
	.binding = 0,
	.stride = sizeof(Vertex),
	.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
};
constexpr std::array<VkVertexInputAttributeDescription, 5> DEFAULT_VERTEX_ATTRIBUTE_DESCRIPTIONS {
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
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.offset = offsetof(Vertex, tangent)
	},
	VkVertexInputAttributeDescription {
		.location = 3,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(Vertex, uv0)
	},
	VkVertexInputAttributeDescription {
		.location = 4,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(Vertex, uv1)
	}
};

struct ImGuiVertex {
	glm::vec2 pos;
	glm::vec2 uv;
	uint32_t col;
};
constexpr VkVertexInputBindingDescription IMGUI_VERTEX_BINDING_DESCRIPTION {
	.binding = 0,
	.stride = sizeof(ImGuiVertex),
	.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
};
constexpr std::array<VkVertexInputAttributeDescription, 3> IMGUI_VERTEX_ATTRIBUTE_DESCRIPTIONS {
	VkVertexInputAttributeDescription {
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(ImGuiVertex, pos)
	},
	VkVertexInputAttributeDescription {
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(ImGuiVertex, uv)
	},
	VkVertexInputAttributeDescription {
		.location = 2,
		.binding = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.offset = offsetof(ImGuiVertex, col)
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

enum class VertexInputState {
	Default,
	Empty,
	ImGui
};
enum class RasterizationState {
	CullClockwise,
	CullCounterClockwise,
	CullNone
};
enum class MultisampleState {
	Off
};
enum class DepthStencilState {
	Off,
	On
};
enum class ColorBlendState {
	Off,
	ImGui
};
enum class DynamicState {
	None,
	Viewport,
	ViewportScissor,
	DepthBias
};

struct PushConstantDescription {
	uint32_t size;
	VkPipelineStageFlags pipeline_stage;
};
inline constexpr PushConstantDescription PUSHCONSTANTS_NONE {
	.size = 0,
	.pipeline_stage = 0
};

struct SpecializationConstantsDescription {
	VkShaderStageFlags shader_stage;
	std::vector<int> specialization_constants;
};

enum class TransientResourceType {
	Image,
	Buffer
};

enum class TransientImageType {
	AttachmentImage,
	SampledImage,
	StorageImage
};

struct TransientImage {
	TransientImageType type;
	uint32_t width;
	uint32_t height;
	VkFormat format;
	uint32_t binding;
	
	ColorBlendState color_blend_state;
};

struct TransientBuffer {
	uint32_t stride;
	uint32_t count;
};

struct TransientResource {
	TransientResourceType type;
	const char *name;

	union {
		TransientImage image;
		TransientBuffer buffer;
	};
};

struct GraphicsPipelineDescription {
	const char *name;
	const char *vertex_shader;
	const char *fragment_shader;
	VertexInputState vertex_input_state;
	MultisampleState multisample_state;
	DepthStencilState depth_stencil_state;
	DynamicState dynamic_state;
	PushConstantDescription push_constants;
	SpecializationConstantsDescription specialization_constants_description;
};

struct GraphicsPipeline {
	GraphicsPipelineDescription description;
	VkPipeline handle;
	VkPipelineLayout layout;
};

struct HitShader {
	const char *closest_hit;
	const char *any_hit;
};

struct RaytracingPipelineDescription {
	const char *name;
	const char *raygen_shader;
	std::vector<const char *> miss_shaders;
	std::vector<HitShader> hit_shaders;
};

struct ShaderBindingTable {
	MappedBuffer buffer;
	VkStridedDeviceAddressRegionKHR strided_device_address_region;
};

struct RaytracingPipeline {
	RaytracingPipelineDescription description;
	uint32_t shader_group_size;
	ShaderBindingTable raygen_sbt;
	ShaderBindingTable miss_sbt;
	ShaderBindingTable hit_sbt;
	VkPipeline handle;
	VkPipelineLayout layout;
};

class GraphicsExecutionContext;
using GraphicsExecutionCallback = std::function<void(GraphicsExecutionContext &)>;
using ExecuteGraphicsCallback = std::function<void(std::string, GraphicsExecutionCallback)>;
using GraphicsPassCallback = std::function<void(ExecuteGraphicsCallback)>;

class RaytracingExecutionContext;
using RaytracingExecutionCallback = std::function<void(RaytracingExecutionContext &)>;
using ExecuteRaytracingCallback = std::function<void(std::string, RaytracingExecutionCallback)>;
using RaytracingPassCallback = std::function<void(ExecuteRaytracingCallback)>;

struct GraphicsPass {
	VkRenderPass handle;
	std::vector<TransientResource> attachments;
	std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers;
	GraphicsPassCallback callback;
};

struct ImageAccess {
	VkImageLayout layout;
	VkAccessFlags access_flags;
	VkPipelineStageFlags stage_flags;
};

struct RaytracingPass {
	RaytracingPassCallback callback;
};

struct RenderPass {
	const char *name;

	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_set;
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
	const char *name;
	std::vector<TransientResource> dependencies;
	std::vector<TransientResource> outputs;

	std::variant<GraphicsPassDescription, RaytracingPassDescription> description;
};

enum class RenderPathState {
	Idle,
	ChangeToHybrid,
	ChangeToRayquery,
	ChangeToRaytraced
};

struct UserInterfaceState {
	RenderPathState render_path_state;
	std::string debug_texture;
};
