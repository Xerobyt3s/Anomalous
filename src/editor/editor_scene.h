#pragma once

#include "carsys/items.h"
#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"
#include "world/entity.h"

#include <span>
#include <string_view>

namespace anom {

class DebugDraw;
class Editor;
class Input;
class InteractBoxes;
class PhysWorld;
class Terrain;
class TapeLibrary;
class TextRenderer;
class Ui;
class Vehicle;
class World;
struct Camera;

inline constexpr u32 kSceneMaxMeshes = 128;
inline constexpr f32 kScenePanelWidth = 260.0f;
inline constexpr i32 kSceneListPage = 12;

enum class SpawnKind : u32 {
    Static,
    Tree,
    Building,
};

class EditorScene {
public:
    void reset();

    bool update(Editor& editor, const Input& input, const Camera& cam, World& world,
                PhysWorld& phys, const Terrain& terrain, Vehicle* veh, InteractBoxes* boxes,
                Vec2 viewport, bool over_panel);

    std::span<const FixedString<32>> meshes() const { return {meshes_, mesh_count_}; }

    i32 place_mesh() const { return place_mesh_; }
    void set_place_mesh(i32 index);
    i32 place_item() const { return place_item_; }
    void set_place_item(i32 kind);
    void clear_placement();

    SpawnKind spawn_kind() const { return spawn_kind_; }
    void cycle_spawn_kind();

    bool car_boxes_on() const { return car_boxes_on_; }
    void set_car_boxes_on(bool on) { car_boxes_on_ = on; }
    i32 box_selection() const { return box_sel_; }
    void set_box_selection(i32 index) { box_sel_ = index; }

    bool preview_valid() const { return preview_valid_; }
    Vec3 preview_pos() const { return preview_pos_; }

    void render(Editor& editor, Ui& ui, DebugDraw& debug, const Camera& cam, World& world,
                PhysWorld& phys, const Terrain& terrain, Vehicle* veh, InteractBoxes* boxes,
                const TapeLibrary* tapes, Vec2 viewport);

    EntityHandle add_trigger(Editor& editor, const Camera& cam, World& world, PhysWorld& phys,
                             const Terrain& terrain);
    EntityHandle add_gravity(Editor& editor, const Camera& cam, World& world, PhysWorld& phys);

private:
    bool update_car_boxes(Editor& editor, const Input& input, const Camera& cam, PhysWorld& phys,
                          Vehicle& veh, InteractBoxes& boxes, Vec2 viewport, bool over_panel);
    void palette_panel(Editor& editor, Ui& ui, const Camera& cam, World& world,
                       PhysWorld& phys, const Terrain& terrain, f32 px);
    void outliner_panel(Editor& editor, Ui& ui, World& world, f32 px);
    void detail_panel(Editor& editor, Ui& ui, World& world, InteractBoxes* boxes,
                      const TapeLibrary* tapes);

    bool update_placement(Editor& editor, const Input& input, const Camera& cam, World& world,
                          PhysWorld& phys, Vec2 viewport, bool over_panel);

    FixedString<32> meshes_[kSceneMaxMeshes];
    u32 mesh_count_ = 0;

    i32 place_mesh_ = -1;
    i32 place_item_ = -1;
    SpawnKind spawn_kind_ = SpawnKind::Static;
    bool car_boxes_on_ = false;
    i32 box_sel_ = -1;

    bool preview_valid_ = false;
    Vec3 preview_pos_;

    i32 palette_tab_ = 0;
    i32 mesh_scroll_ = 0;
    i32 outliner_scroll_ = 0;
    i32 tape_scroll_ = 0;
    EntityHandle trigger_sync_;
    char trigger_name_[32] = {};
};

} // namespace anom
