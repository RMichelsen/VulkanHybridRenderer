#include "pch.h"
#include "shader_compiler.h"

namespace VkUtils {
std::vector<uint32_t> CompileShader(const char *path, VkShaderStageFlags shader_stage) {
	assert(shader_stage == VK_SHADER_STAGE_VERTEX_BIT || 
		   shader_stage == VK_SHADER_STAGE_FRAGMENT_BIT || 
		   shader_stage == VK_SHADER_STAGE_COMPUTE_BIT);

	std::ifstream f(path);
	std::stringstream buffer;
	buffer << f.rdbuf();
	std::string shader_string = buffer.str();

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    // options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc_shader_kind shader_kind;
    if(shader_stage == VK_SHADER_STAGE_VERTEX_BIT) {
        shader_kind = shaderc_shader_kind::shaderc_glsl_vertex_shader;
    }
    else if(shader_stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        shader_kind = shaderc_shader_kind::shaderc_glsl_fragment_shader;
    }
    else if(shader_stage == VK_SHADER_STAGE_COMPUTE_BIT) {
        shader_kind = shaderc_shader_kind::shaderc_glsl_compute_shader;
    }

    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(shader_string, shader_kind, 
        path, options);
    if(result.GetCompilationStatus() != shaderc_compilation_status_success) {
        printf("Shader Compile Error: %s\n", result.GetErrorMessage().c_str());
        assert(false);
    }

    return { result.cbegin(), result.cend() };
}
}