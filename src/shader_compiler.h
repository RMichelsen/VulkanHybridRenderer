#pragma once

namespace ShaderCompiler {
std::vector<uint32_t> CompileShader(const char *path, VkShaderStageFlags shader_stage);
}
