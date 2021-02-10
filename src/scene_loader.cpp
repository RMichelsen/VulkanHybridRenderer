#include "pch.h"
#include "scene_loader.h"

#include "resource_manager.h"

namespace SceneLoader {
VkFilter GetVkFilter(cgltf_int filter) {
	switch(filter) {
	case 0x2700:
	case 0x2702:
		return VK_FILTER_NEAREST;
	case 0x2701:
	case 0x2703:
		return VK_FILTER_LINEAR;
	default:
		return VK_FILTER_LINEAR;
	}
}

VkSamplerAddressMode GetVkAddressMode(cgltf_int address_mode) {
	switch(address_mode) {
	case 0x2900:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case 0x2901:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	default:
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
		scene.camera.view = glm::inverse(scene.camera.view);
		return;
	}

	if(!node.mesh) {
		return;
	}

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
			if(normal_accessor) {
				cgltf_accessor_read_float(normal_accessor, j, 
					reinterpret_cast<cgltf_float *>(glm::value_ptr(v.normal)), 3);
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

	// TODO: Redo parallelization...
	struct ImageData {
		int id;
		int x;
		int y;
		cgltf_sampler *sampler;
		uint8_t *data;
	};

	std::vector<ImageData> images(data->textures_count);

	int count = 0;
	for(ImageData &image : images) {
		image.id = count++;
	}

	std::for_each(std::execution::par, images.begin(), images.end(), 
		[&](ImageData &image) {
			cgltf_texture *texture = &data->textures[image.id];
			int _;
			if(texture->image->uri) {
				image.sampler = texture->sampler;
				std::string texture_path = parent_path + texture->image->uri;
				image.data = stbi_load(texture_path.c_str(), &image.x, &image.y, &_, STBI_rgb_alpha);
			}
			else {
				image.sampler = texture->sampler;
				uint64_t size = texture->image->buffer_view->size;
				uint64_t offset = texture->image->buffer_view->offset;
				uint8_t *buffer = reinterpret_cast<uint8_t *>(texture->image->buffer_view->buffer->data) + offset;
				image.data = stbi_load_from_memory(buffer, static_cast<int>(size), &image.x, &image.y, &_, STBI_rgb_alpha);
			}

			assert(image.data);
		}
	);

	for(int i = 0; i < data->textures_count; ++i) {
		ImageData &image = images[i];
		SamplerInfo sampler_info {
			.mag_filter = GetVkFilter(image.sampler->mag_filter),
			.min_filter = GetVkFilter(image.sampler->min_filter),
			.address_mode_u = GetVkAddressMode(image.sampler->wrap_s),
			.address_mode_v = GetVkAddressMode(image.sampler->wrap_t),
		};
		textures[data->textures[i].image->name] =
			resource_manager.UploadTextureFromData(image.x, image.y, image.data, &sampler_info);
		free(images[i].data);
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	for(int i = 0; i < data->nodes_count; ++i) {
		ParseNode(data->nodes[i], scene, textures, vertices, indices);
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

