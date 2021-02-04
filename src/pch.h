#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <array>
#include <execution>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "cgltf/cgltf.h"
#include "stb/stb_image.h"
#include "vma/vk_mem_alloc.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/scalar_constants.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/string_cast.hpp"
#include "glm/gtx/quaternion.hpp"
 
#include "vulkan_common.h"
#include "vulkan_pipeline_presets.h"
