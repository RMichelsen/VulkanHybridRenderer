#pragma once

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
