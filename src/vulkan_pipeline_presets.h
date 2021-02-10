#pragma once

inline constexpr VkPipelineVertexInputStateCreateInfo VERTEX_INPUT_STATE_DEFAULT {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = 1,
	.pVertexBindingDescriptions = &DEFAULT_VERTEX_BINDING_DESCRIPTION,
	.vertexAttributeDescriptionCount = static_cast<uint32_t>(DEFAULT_VERTEX_ATTRIBUTE_DESCRIPTIONS.size()),
	.pVertexAttributeDescriptions = DEFAULT_VERTEX_ATTRIBUTE_DESCRIPTIONS.data()
};

inline constexpr VkPipelineVertexInputStateCreateInfo VERTEX_INPUT_STATE_IMGUI {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = 1,
	.pVertexBindingDescriptions = &IMGUI_VERTEX_BINDING_DESCRIPTION,
	.vertexAttributeDescriptionCount = static_cast<uint32_t>(IMGUI_VERTEX_ATTRIBUTE_DESCRIPTIONS.size()),
	.pVertexAttributeDescriptions = IMGUI_VERTEX_ATTRIBUTE_DESCRIPTIONS.data()
};

inline constexpr VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_FILL {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	.polygonMode = VK_POLYGON_MODE_FILL,
	.cullMode = VK_CULL_MODE_FRONT_BIT,
	.frontFace = VK_FRONT_FACE_CLOCKWISE,
	.lineWidth = 1.0f
};
inline constexpr VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_WIREFRAME {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	.polygonMode = VK_POLYGON_MODE_LINE,
	.cullMode = VK_CULL_MODE_FRONT_BIT,
	.frontFace = VK_FRONT_FACE_CLOCKWISE,
	.lineWidth = 1.0f
};
inline constexpr VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_FILL_CULL_CCW {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	.polygonMode = VK_POLYGON_MODE_FILL,
	.cullMode = VK_CULL_MODE_FRONT_BIT,
	.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	.lineWidth = 1.0f
};
inline constexpr VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_FILL_NOCULL_CCW {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	.polygonMode = VK_POLYGON_MODE_FILL,
	.cullMode = VK_CULL_MODE_NONE,
	.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	.lineWidth = 1.0f
};
inline constexpr VkPipelineMultisampleStateCreateInfo MULTISAMPLE_STATE_OFF {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
	.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
};
inline constexpr VkPipelineDepthStencilStateCreateInfo DEPTH_STENCIL_STATE_ON {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
	.depthTestEnable = VK_TRUE,
	.depthWriteEnable = VK_TRUE,
	.depthCompareOp = VK_COMPARE_OP_LESS,
};
inline constexpr VkPipelineDepthStencilStateCreateInfo DEPTH_STENCIL_STATE_OFF {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
	.depthTestEnable = VK_FALSE,
	.depthWriteEnable = VK_FALSE
};
inline constexpr VkPipelineColorBlendAttachmentState COLOR_BLEND_ATTACHMENT_STATE_OFF {
	.blendEnable = VK_FALSE,
	.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
};
inline constexpr VkPipelineColorBlendAttachmentState COLOR_BLEND_ATTACHMENT_STATE_IMGUI {
	.blendEnable = VK_TRUE,
	.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
	.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	.colorBlendOp = VK_BLEND_OP_ADD,
	.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
	.alphaBlendOp = VK_BLEND_OP_ADD,
	.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};
inline constexpr std::array<VkDynamicState, 2> VIEWPORT_SCISSOR_STATES = {
	VK_DYNAMIC_STATE_VIEWPORT,
	VK_DYNAMIC_STATE_SCISSOR
};
inline constexpr VkPipelineDynamicStateCreateInfo DYNAMIC_STATE_VIEWPORT_SCISSOR {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	.dynamicStateCount = static_cast<uint32_t>(VIEWPORT_SCISSOR_STATES.size()),
	.pDynamicStates = VIEWPORT_SCISSOR_STATES.data()
};

