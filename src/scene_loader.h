#pragma once

class ResourceManager;
namespace SceneLoader {
Scene LoadScene(ResourceManager &resource_manager, const char *path);
void ParseNode(ResourceManager &resource_manager, cgltf_node *node, glm::mat4 translation);
void ParseglTF(ResourceManager &resource_manager, const char *path, cgltf_data *data);

void DestroyScene(ResourceManager &resource_manager, Scene &scene);
}
