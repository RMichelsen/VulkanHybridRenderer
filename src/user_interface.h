#pragma once

struct ImGuiPushConstants {
	glm::vec2 scale;
	glm::vec2 translate;
	uint32_t font_texture;
};

inline constexpr GraphicsPipelineDescription IMGUI_PIPELINE_DESCRIPTION {
	.name = "ImGui Pipeline",
	.vertex_shader = "data/shaders/compiled/imgui.vert.spv",
	.fragment_shader = "data/shaders/compiled/imgui.frag.spv",
	.vertex_input_state = VertexInputState::ImGui,
	.rasterization_state = RasterizationState::FillNoCullCCW,
	.multisample_state = MultisampleState::Off,
	.depth_stencil_state = DepthStencilState::Off,
	.dynamic_state = DynamicState::ViewportScissor,
	.push_constants = PushConstantDescription {
		.size = sizeof(ImGuiPushConstants),
		.pipeline_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
	}
};

class RenderGraph;
class ResourceManager;
class VulkanContext;
class UserInterface {
public:
	UserInterface(VulkanContext &context, ResourceManager &resource_manager);
	void DestroyResources();

	void Update();
	void Draw(GraphicsExecutionContext &execution_context);

private:
	VulkanContext &context;

	MappedBuffer vertex_buffer;
	MappedBuffer index_buffer;
	uint32_t font_texture;
};

