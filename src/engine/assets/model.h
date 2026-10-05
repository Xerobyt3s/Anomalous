#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ghost::engine {
struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
};

struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

struct ModelNode {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    int mesh = -1;
    glm::mat4 bindLocal{1.0f};
};

struct ModelData {
    std::vector<ModelNode> nodes;
    std::vector<int> roots;
    std::vector<MeshData> meshes;

    int findNode(std::string_view name) const;
    std::vector<glm::mat4> bindLocals() const;

    std::vector<glm::mat4> computeWorld(std::span<const glm::mat4> locals) const;
};

ModelData loadGltf(const std::filesystem::path& path);

}
