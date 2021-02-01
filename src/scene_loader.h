#pragma once

class ResourceManager;
namespace SceneLoader {
Scene LoadScene(ResourceManager &resource_manager, const char *path);
void ParseNode(cgltf_node &node, Scene &scene, std::unordered_map<const char *, int> &textures,
	std::vector<Vertex> &vertices, std::vector<uint32_t> &indices);
void ParseglTF(ResourceManager &resource_manager, const char *path, cgltf_data *data);

void DestroyScene(ResourceManager &resource_manager, Scene &scene);
}
