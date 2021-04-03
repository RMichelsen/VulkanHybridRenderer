#include "pch.h"
#include "scene_loader.h"

#include "rendering_backend/resource_manager.h"

namespace SceneLoader {
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
		cgltf_node_transform_world(&node, glm::value_ptr(scene.camera.transform));
		float yaw, pitch, roll;
		glm::extractEulerAngleYXZ(scene.camera.transform, yaw, pitch, roll);
		glm::mat4 R = glm::yawPitchRoll(yaw, pitch, roll);
		glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(glm::column(scene.camera.transform, 3)));
		scene.camera.transform = T * R;
		scene.camera.view = glm::inverse(scene.camera.transform);
		scene.camera.yaw = yaw;
		scene.camera.pitch = pitch;
		scene.camera.roll = roll;
		return;
	}

	if(node.light && node.light->type == cgltf_light_type_directional) {
		glm::mat4 node_transform;
		cgltf_node_transform_world(&node, glm::value_ptr(node_transform));

		glm::vec3 translation;
		glm::vec3 skew;
		glm::vec3 scale;
		glm::quat rot;
		glm::vec4 persp;
		glm::decompose(node_transform, scale, rot, translation, skew, persp);

		// TODO: Might want to compute bounding box of scene
		glm::mat4 light_perspective = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 64.0f);
		glm::vec3 light_direction = glm::rotate(rot, glm::vec3(0.0f, 0.0f, -1.0f));
		glm::mat4 light_view = glm::lookAt(
			-glm::normalize(light_direction) * 60.0f,
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);
		scene.directional_light = DirectionalLight {
			.projview = light_perspective * light_view,
			.direction = glm::vec4(light_direction, 1.0f),
			.color = glm::vec4(glm::make_vec3(node.light->color), 1.0f)
		};
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
		cgltf_accessor *tangent_accessor = nullptr;
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
			else if(primitive->attributes[j].type == cgltf_attribute_type_tangent) {
				tangent_accessor = accessor;
				assert(accessor->type == cgltf_type_vec4);
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
			if(tangent_accessor) {
				cgltf_accessor_read_float(tangent_accessor, j,
					reinterpret_cast<cgltf_float *>(glm::value_ptr(v.tangent)), 4);
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

		Material material {
			.base_color = glm::vec4(1.0f),
			.base_color_texture = -1,
			.metallic_roughness_texture = -1,
			.normal_map = -1,
			.metallic_factor = 1.0f,
			.roughness_factor = 1.0f,
			.alpha_mask = 0,
			.alpha_cutoff = 0.0f
		};


		cgltf_texture_view albedo = 
			primitive->material->pbr_metallic_roughness.base_color_texture;
		if(albedo.texture) {
			material.base_color_texture = textures[albedo.texture->image->name];
		}
		else {
			material.base_color = glm::make_vec4(primitive->material->pbr_metallic_roughness.base_color_factor);
		}

		assert(primitive->material->has_pbr_metallic_roughness && "Only glTF PBR Metallic Roughness model is supported");
		cgltf_texture_view metallic_roughness =
			primitive->material->pbr_metallic_roughness.metallic_roughness_texture;
		if(metallic_roughness.texture) {
			material.metallic_roughness_texture = textures[metallic_roughness.texture->image->name];
		}
		else {
			material.metallic_factor = primitive->material->pbr_metallic_roughness.metallic_factor;
			material.roughness_factor = primitive->material->pbr_metallic_roughness.roughness_factor;
		}

		if(primitive->material->normal_texture.texture) {
			material.normal_map = textures[primitive->material->normal_texture.texture->image->name];

			// TODO: Handle case of no vertex tangents, but normal map present
			assert(tangent_accessor);
		}
		if(primitive->material->alpha_mode == cgltf_alpha_mode_mask) {
			material.alpha_mask = 1;
			material.alpha_cutoff = primitive->material->alpha_cutoff;
		}

		mesh.primitives.push_back(Primitive {
			.transform = transform,
			.material = material,
			.vertex_offset = vertex_offset,
			.index_offset = index_offset,
			.index_count = static_cast<uint32_t>(primitive->indices->count)
		});
	}

	scene.meshes.push_back(mesh);
}

void UploadTexture(ResourceManager &resource_manager, cgltf_texture *texture, VkFormat format) {

}

void ParseglTF(ResourceManager &resource_manager, const char *path, cgltf_data *data, Scene &scene) {
	cgltf_options options {};
	cgltf_load_buffers(&options, data, path);

	std::string parent_path = std::filesystem::path(path).parent_path().string() + "/";

	// Collect all textures from model, and associate the correct format to them.
	// This is instead of doing the SRGB to linear conversion in the shaders.
	std::vector<std::pair<cgltf_texture *, VkFormat>> textures_to_upload;
	for(int i = 0; i < data->meshes_count; ++i) {
		for(int j = 0; j < data->meshes[i].primitives_count; ++j) {
			if(data->meshes[i].primitives[j].material->has_pbr_metallic_roughness) {
				cgltf_pbr_metallic_roughness *metallic_roughness = &data->meshes[i].primitives[j].material->pbr_metallic_roughness;
				if(metallic_roughness->base_color_texture.texture) {
					textures_to_upload.emplace_back(std::make_pair(
						metallic_roughness->base_color_texture.texture,
						VK_FORMAT_R8G8B8A8_SRGB
					));
				}
				if(metallic_roughness->metallic_roughness_texture.texture) {
					textures_to_upload.emplace_back(std::make_pair(
						metallic_roughness->metallic_roughness_texture.texture,
						VK_FORMAT_R8G8B8A8_SRGB
					));
				}
			}

			if(data->meshes[i].primitives[j].material->normal_texture.texture) {
				textures_to_upload.emplace_back(std::make_pair(
					data->meshes[i].primitives[j].material->normal_texture.texture,
					VK_FORMAT_R8G8B8A8_UNORM
				));
			}
		}
	}

	std::unordered_map<const char *, int> textures;
	#pragma omp parallel for
	for(int i = 0; i < textures_to_upload.size(); ++i) {
		auto &[texture, format] = textures_to_upload[i];
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
			image_idx = resource_manager.UploadTextureFromData(x, y, image_data, format, &sampler_info);
			textures[texture->image->name] = image_idx;
			free(image_data);
		}
		resource_manager.TagImage(image_idx, texture->image->name);
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	for(int i = 0; i < data->nodes_count; ++i) {
		ParseNode(data->nodes[i], scene, textures, vertices, indices);
	}

	uint32_t num_directional_lights = 0;
	for(int i = 0; i < data->lights_count; ++i) {
		if(data->lights[i].type == cgltf_light_type_directional) {
			num_directional_lights++;
		}
	}

	if(num_directional_lights == 0) {
		scene.directional_light = DirectionalLight {
			.direction = glm::vec4(0.0f, -1.0f, 0.01f, 0.0f),
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

