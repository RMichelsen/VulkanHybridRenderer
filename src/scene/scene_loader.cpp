#include "pch.h"
#include "scene_loader.h"

#include "rendering_backend/resource_manager.h"

namespace SceneLoader {
constexpr glm::mat4 REFLECT_Y {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, -1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};

VkFilter GetVkFilter(cgltf_int filter) {
	switch(filter) {
	case 0x2600:
	case 0x2700:
	case 0x2701:
		return VK_FILTER_NEAREST;
	case 0x2601:
	case 0x2702:
	case 0x2703:
		return VK_FILTER_LINEAR;
	default:
		assert(false);
		return VK_FILTER_LINEAR;
	}
}

VkSamplerAddressMode GetVkAddressMode(cgltf_int address_mode) {
	switch(address_mode) {
	case 0x812F:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case 0x812D:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case 0x2901:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case 0x8370:
		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	default:
		assert(false);
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	}
}

void ParseNode(cgltf_node &node, Scene &scene, std::unordered_map<const char *, int> &textures, 
	std::vector<Vertex> &vertices, std::vector<uint32_t> &indices) {

	if(node.camera) {
		if(node.camera->type == cgltf_camera_type_perspective) {
			scene.camera.perspective = glm::perspective(
				node.camera->data.perspective.yfov,
				node.camera->data.perspective.aspect_ratio,
				node.camera->data.perspective.znear,
				node.camera->data.perspective.zfar
			);
		}
		else if(node.camera->type == cgltf_camera_type_orthographic) {
			scene.camera.perspective = glm::ortho(
				-node.camera->data.orthographic.xmag,
				node.camera->data.orthographic.xmag,
				-node.camera->data.orthographic.ymag,
				node.camera->data.orthographic.ymag,
				node.camera->data.orthographic.znear,
				node.camera->data.orthographic.zfar
			);
		}
		cgltf_node_transform_world(&node, glm::value_ptr(scene.camera.view));
		scene.camera.perspective[1][1] *= -1.0f;

		// TODO: Decompose matrix into TRS to allow camera controls to work from a given initial view.
		//glm::vec3 translation = glm::vec3(scene.camera.view[3][0], scene.camera.view[3][1], scene.camera.view[3][2]);
		//scene.camera.view[3][0] = 0.0f;
		//scene.camera.view[3][1] = 0.0f;
		//scene.camera.view[3][2] = 0.0f;
		//glm::vec3 scale = glm::vec3(
		//	glm::length(glm::vec3(scene.camera.view[0][0], scene.camera.view[0][1], scene.camera.view[0][2])),
		//	glm::length(glm::vec3(scene.camera.view[1][0], scene.camera.view[1][1], scene.camera.view[1][2])),
		//	glm::length(glm::vec3(scene.camera.view[2][0], scene.camera.view[2][1], scene.camera.view[2][2]))
		//);
		//scene.camera.view[0][0] /= scale.x;
		//scene.camera.view[0][1] /= scale.x;
		//scene.camera.view[0][2] /= scale.x;
		//scene.camera.view[1][0] /= scale.y;
		//scene.camera.view[1][1] /= scale.y;
		//scene.camera.view[1][2] /= scale.y;
		//scene.camera.view[2][0] /= scale.z;
		//scene.camera.view[2][1] /= scale.z;
		//scene.camera.view[2][2] /= scale.z;

		////scene.camera.view[0][0] *= -1.0f;
		////scene.camera.view[0][1] *= -1.0f;
		////scene.camera.view[0][2] *= -1.0f;

		//glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
		//glm::mat4 R = scene.camera.view;
		//glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
		
		// Reflect transformation matrix in Y axis
		scene.camera.view = glm::inverse(REFLECT_Y * scene.camera.view);
		return;
	}

	if(node.light) {
		assert(node.light->type == cgltf_light_type_directional);

		glm::mat4 node_transform;
		cgltf_node_transform_world(&node, glm::value_ptr(node_transform));
		node_transform = REFLECT_Y * node_transform;

		// TODO: Get bounding box for shadowmap
		glm::mat4 light_perspective = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 64.0f);
		light_perspective[1][1] *= -1.0f;
		glm::mat4 light_view = glm::lookAt(
			glm::vec3(0.0f, -60.0f, 0.1f),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, -1.0f, 0.0f)
		);
		scene.directional_light = DirectionalLight {
			.projview = light_perspective * light_view,
			.direction = glm::vec4(glm::mat3(node_transform) * glm::vec3(0.0f, 0.0f, -1.0f), 1.0f),
			.color = glm::vec4(glm::make_vec3(node.light->color), 1.0f)
		};
	}

	if(!node.mesh) {
		return;
	}

	// TODO: Fix reflection in Y plane
	glm::mat4 transform;
	cgltf_node_transform_world(&node, glm::value_ptr(transform));

	Mesh mesh;
	for(int i = 0; i < node.mesh->primitives_count; ++i) {
		cgltf_primitive *primitive = &node.mesh->primitives[i];
		assert(primitive->type == cgltf_primitive_type_triangles);

		uint32_t vertex_offset = static_cast<uint32_t>(vertices.size()); 
		uint32_t index_offset = static_cast<uint32_t>(indices.size()); 

		cgltf_accessor *position_accessor = nullptr;
		cgltf_accessor *normal_accessor = nullptr;
		cgltf_accessor *uv0_accessor = nullptr;
		cgltf_accessor *uv1_accessor = nullptr;

		for(int j = 0; j < primitive->attributes_count; ++j) {
			cgltf_accessor *accessor = primitive->attributes[j].data;

			if(primitive->attributes[j].type == cgltf_attribute_type_position) {
				position_accessor = accessor;
				assert(accessor->type == cgltf_type_vec3);
			}
			else if(primitive->attributes[j].type == cgltf_attribute_type_normal) {
				normal_accessor = accessor;
				assert(accessor->type == cgltf_type_vec3);
			}
			else if(primitive->attributes[j].type == cgltf_attribute_type_texcoord) {
				if(primitive->attributes[j].index == 0) {
					uv0_accessor = accessor;
				}
				else if(primitive->attributes[j].index == 1) {
					uv1_accessor = accessor;
				}
				assert(accessor->type == cgltf_type_vec2);
			}
		}

		assert(position_accessor);
		for(int j = 0; j < position_accessor->count; ++j) {
			Vertex v {};
			cgltf_accessor_read_float(position_accessor, j, 
				reinterpret_cast<cgltf_float *>(glm::value_ptr(v.pos)), 3);
			v.pos.y *= -1.0f;
			if(normal_accessor) {
				cgltf_accessor_read_float(normal_accessor, j, 
					reinterpret_cast<cgltf_float *>(glm::value_ptr(v.normal)), 3);
				v.normal.y *= -1.0f;
			}
			if(uv0_accessor) {
				cgltf_accessor_read_float(uv0_accessor, j, 
					reinterpret_cast<cgltf_float *>(glm::value_ptr(v.uv0)), 2);
			}
			if(uv1_accessor) {
				cgltf_accessor_read_float(uv1_accessor, j, 
					reinterpret_cast<cgltf_float *>(glm::value_ptr(v.uv1)), 2);
			}
			vertices.emplace_back(v);
		}

		assert(primitive->indices);

		for(int j = 0; j < primitive->indices->count; ++j) {
			uint32_t index = static_cast<uint32_t>(cgltf_accessor_read_index(primitive->indices, j));
			indices.emplace_back(index);
		}

		int texture = -1;
		if(primitive->material->has_pbr_metallic_roughness) {
			cgltf_texture_view albedo = 
				primitive->material->pbr_metallic_roughness.base_color_texture;
			if(albedo.texture) {
				texture = textures[albedo.texture->image->name];
			}
		}
		else if(primitive->material->has_pbr_specular_glossiness) {
			cgltf_texture_view albedo = 
				primitive->material->pbr_specular_glossiness.diffuse_texture;
			if(albedo.texture) {
				texture = textures[albedo.texture->image->name];
			}
		}

		mesh.primitives.push_back(Primitive {
			.transform = transform,
			.vertex_offset = vertex_offset,
			.index_offset = index_offset,
			.index_count = static_cast<uint32_t>(primitive->indices->count),
			.texture = texture
		});
	}

	scene.meshes.push_back(mesh);
}

void ParseglTF(ResourceManager &resource_manager, const char *path, cgltf_data *data,
	Scene &scene) {
	cgltf_options options {};
	cgltf_load_buffers(&options, data, path);

	std::string parent_path = std::filesystem::path(path).parent_path().string() + "/";

	std::unordered_map<const char *, int> textures;

	#pragma omp parallel for
	for(int i = 0; i < data->textures_count; ++i) {
		cgltf_texture *texture = &data->textures[i];
		int _, x, y;
		uint8_t *image_data;
		if(texture->image->uri) {
			std::string texture_path = parent_path + texture->image->uri;
			image_data = stbi_load(texture_path.c_str(), &x, &y, &_, STBI_rgb_alpha);
		}
		else {
			uint64_t size = texture->image->buffer_view->size;
			uint64_t offset = texture->image->buffer_view->offset;
			uint8_t *buffer = reinterpret_cast<uint8_t *>(texture->image->buffer_view->buffer->data) + offset;
			image_data = stbi_load_from_memory(buffer, static_cast<int>(size), &x, &y, &_, STBI_rgb_alpha);
		}
		assert(image_data);

		SamplerInfo sampler_info {
			.mag_filter = GetVkFilter(texture->sampler->mag_filter),
			.min_filter = GetVkFilter(texture->sampler->min_filter),
			.address_mode_u = GetVkAddressMode(texture->sampler->wrap_s),
			.address_mode_v = GetVkAddressMode(texture->sampler->wrap_t)
		};

		uint32_t image_idx;
		#pragma omp critical
		{
			image_idx = resource_manager.UploadTextureFromData(x, y, image_data, VK_FORMAT_R8G8B8A8_UNORM, &sampler_info);
			textures[data->textures[i].image->name] = image_idx;
			free(image_data);
		}
		resource_manager.TagImage(image_idx, data->textures[i].image->name);
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	for(int i = 0; i < data->nodes_count; ++i) {
		ParseNode(data->nodes[i], scene, textures, vertices, indices);
	}

	if(data->lights_count == 0) {
		scene.directional_light = DirectionalLight{
			.direction = glm::vec4(0.45f, -1.0f, 0.45f, 0.0f),
			.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)
		};
	}

	resource_manager.UpdateGeometry(vertices, indices, scene);
}

Scene LoadScene(ResourceManager &resource_manager, const char *path) {
	Scene scene {};

	cgltf_options options {};
	cgltf_data *data = nullptr;
	cgltf_result result = cgltf_parse_file(&options, path, &data);
	if(result == cgltf_result_success) {
		ParseglTF(resource_manager, path, data, scene);
	}
	else {
		printf("Error Parsing glTF 2.0 File\n");
	}

	return scene;
}
}

