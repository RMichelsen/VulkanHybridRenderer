#pragma once

namespace VkUtils {
std::vector<uint32_t> CompileShader(const char *path, VkShaderStageFlags shader_stage);
}
