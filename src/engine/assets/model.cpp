#include "engine/assets/model.h"

#include "engine/assets/asset_path.h"
#include "engine/debug/log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

#include <cfloat>
#include <memory>
#include <stdexcept>

namespace ghost::engine {
namespace {
MeshData loadMesh(const cgltf_mesh& mesh) {
    MeshData out;
    out.name = mesh.name ? mesh.name : "";
    out.boundsMin = glm::vec3(FLT_MAX);
    out.boundsMax = glm::vec3(-FLT_MAX);

    for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
        const cgltf_primitive& prim = mesh.primitives[p];
        if (prim.type != cgltf_primitive_type_triangles) {
            continue;
        }

        const cgltf_accessor* positions = nullptr;
        const cgltf_accessor* normals = nullptr;
        const cgltf_accessor* uvs = nullptr;
        for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
            const cgltf_attribute& attr = prim.attributes[a];
            if (attr.type == cgltf_attribute_type_position) {
                positions = attr.data;
            } else if (attr.type == cgltf_attribute_type_normal) {
                normals = attr.data;
            } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
                uvs = attr.data;
            }
        }
        if (!positions) {
            continue;
        }

        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.resize(base + positions->count);
        for (cgltf_size v = 0; v < positions->count; ++v) {
            Vertex& vert = out.vertices[base + v];
            cgltf_accessor_read_float(positions, v, &vert.position.x, 3);
            if (normals) {
                cgltf_accessor_read_float(normals, v, &vert.normal.x, 3);
            }
            if (uvs) {
                cgltf_accessor_read_float(uvs, v, &vert.uv.x, 2);
            }
            out.boundsMin = glm::min(out.boundsMin, vert.position);
            out.boundsMax = glm::max(out.boundsMax, vert.position);
        }

        if (prim.indices) {
            for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                out.indices.push_back(base + static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
            }
        } else {
            for (cgltf_size i = 0; i < positions->count; ++i) {
                out.indices.push_back(base + static_cast<std::uint32_t>(i));
            }
        }
    }

    if (out.vertices.empty()) {
        out.boundsMin = out.boundsMax = glm::vec3(0.0f);
    }
    return out;
}

}

int ModelData::findNode(std::string_view name) const {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<glm::mat4> ModelData::bindLocals() const {
    std::vector<glm::mat4> locals;
    locals.reserve(nodes.size());
    for (const ModelNode& node : nodes) {
        locals.push_back(node.bindLocal);
    }
    return locals;
}

std::vector<glm::mat4> ModelData::computeWorld(std::span<const glm::mat4> locals) const {
    std::vector<glm::mat4> world(nodes.size(), glm::mat4(1.0f));
    auto visit = [&](auto& self, int index, const glm::mat4& parentWorld) -> void {
        world[index] = parentWorld * locals[index];
        for (int child : nodes[index].children) {
            self(self, child, world[index]);
        }
    };
    for (int root : roots) {
        visit(visit, root, glm::mat4(1.0f));
    }
    return world;
}

ModelData loadGltf(const std::filesystem::path& path) {
    const std::string pathString = path.string();

    const std::optional<std::string> bytes = readAsset(path);
    if (!bytes) {
        throw std::runtime_error("Missing model: " + pathString);
    }
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, bytes->data(), bytes->size(), &raw) != cgltf_result_success) {
        throw std::runtime_error("Failed to parse glTF: " + pathString);
    }
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, &cgltf_free);
    if (cgltf_load_buffers(&options, data.get(), pathString.c_str()) != cgltf_result_success) {
        throw std::runtime_error("Failed to load glTF buffers: " + pathString);
    }
    if (cgltf_validate(data.get()) != cgltf_result_success) {
        throw std::runtime_error("Invalid glTF: " + pathString);
    }

    ModelData model;
    model.meshes.reserve(data->meshes_count);
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        model.meshes.push_back(loadMesh(data->meshes[m]));
    }

    auto nodeIndex = [&](const cgltf_node* node) { return static_cast<int>(node - data->nodes); };

    model.nodes.resize(data->nodes_count);
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& src = data->nodes[n];
        ModelNode& node = model.nodes[n];
        node.name = src.name ? src.name : "";
        node.parent = src.parent ? nodeIndex(src.parent) : -1;
        node.mesh = src.mesh ? static_cast<int>(src.mesh - data->meshes) : -1;
        for (cgltf_size c = 0; c < src.children_count; ++c) {
            node.children.push_back(nodeIndex(src.children[c]));
        }
        cgltf_node_transform_local(&src, glm::value_ptr(node.bindLocal));
    }

    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
            model.roots.push_back(nodeIndex(scene->nodes[i]));
        }
    } else {
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            if (model.nodes[i].parent < 0) {
                model.roots.push_back(static_cast<int>(i));
            }
        }
    }

    GHOST_INFO("Loaded {} ({} nodes, {} meshes)", path.filename().string(), model.nodes.size(),
               model.meshes.size());
    return model;
}

}
