#include "engine/assets/asset_path.h"
#include "engine/assets/model.h"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace ghost::engine;

TEST_CASE("Revolver.glb has the parts the code depends on") {
    const ModelData model = loadGltf(assetPath("models/Revolver.glb"));
    for (const char* name : {"Frame", "Barrel", "Grip", "Crane", "Cylinder", "EjectRod", "Hammer", "Trigger"}) {
        CAPTURE(name);
        CHECK(model.findNode(name) >= 0);
    }

    const int crane = model.findNode("Crane");
    const int cylinder = model.findNode("Cylinder");
    REQUIRE(cylinder >= 0);
    CHECK(model.nodes[cylinder].parent == crane);
}

TEST_CASE("Cylinder origin lies on its rotation axis") {
    const ModelData model = loadGltf(assetPath("models/Revolver.glb"));
    const int cylinder = model.findNode("Cylinder");
    REQUIRE(cylinder >= 0);
    REQUIRE(model.nodes[cylinder].mesh >= 0);

    const MeshData& mesh = model.meshes[model.nodes[cylinder].mesh];
    const glm::vec3 center = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
    CHECK(std::abs(center.x) < 0.0005f);
    CHECK(std::abs(center.y) < 0.0005f);
}

TEST_CASE("Bullet.glb has casing and bullet") {
    const ModelData model = loadGltf(assetPath("models/Bullet.glb"));
    CHECK(model.findNode("Casing") >= 0);
    CHECK(model.findNode("Bullet") >= 0);
}

TEST_CASE("computeWorld composes parent transforms") {
    ModelData model;
    model.nodes.resize(2);
    model.nodes[0].children = {1};
    model.nodes[1].parent = 0;
    model.roots = {0};
    model.nodes[0].bindLocal = glm::translate(glm::mat4(1.0f), {1.0f, 0.0f, 0.0f});
    model.nodes[1].bindLocal = glm::translate(glm::mat4(1.0f), {0.0f, 2.0f, 0.0f});

    const auto world = model.computeWorld(model.bindLocals());
    CHECK(world[1][3].x == doctest::Approx(1.0f));
    CHECK(world[1][3].y == doctest::Approx(2.0f));
}
