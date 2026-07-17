#include "homeworldz/scene.h"
#include "homeworldz/simulation_loop.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

bool near(double value, double expected) {
    if (std::abs(value - expected) < 1e-9) return true;
    std::cerr << "value " << value << ", want " << expected << '\n';
    return false;
}

} // namespace

int main() {
    const auto top = homeworldz::scene::intersect_box(
        {0.0, 0.0, 5.0}, {0.0, 0.0, -5.0}, {0.0, 0.0, 1.0}, {2.0, 2.0, 2.0});
    const auto side = homeworldz::scene::intersect_box(
        {5.0, 0.0, 1.0}, {-5.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, {2.0, 2.0, 2.0});
    if (!top || !near(top->position.z, 2.0) || top->normal.z != 1.0 ||
        !side || !near(side->position.x, 1.0) || side->normal.x != 1.0 ||
        homeworldz::scene::intersect_box(
            {5.0, 5.0, 5.0}, {4.0, 4.0, 4.0}, {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}))
        return 1;
    homeworldz::scene::Entity root;
    root.id = 10;
    root.position = {10.0, 20.0, 30.0};
    homeworldz::scene::Entity child;
    child.id = 11;
    child.position = {12.0, 20.0, 31.0};
    homeworldz::scene::establish_link(child, root);
    if (child.parent_id != root.id || !near(child.local_position.x, 2.0) ||
        !near(child.local_position.y, 0.0) || !near(child.local_position.z, 1.0))
        return 1;
    constexpr double half_root_two = 0.70710678118654752440;
    root.position = {20.0, 30.0, 40.0};
    root.rotation.z = half_root_two;
    homeworldz::scene::update_linked_world_transform(child, root);
    if (!near(child.position.x, 20.0) || !near(child.position.y, 32.0) ||
        !near(child.position.z, 41.0) || !near(child.rotation.z, half_root_two))
        return 1;
    child.local_position = {0.0, -3.0, 2.0};
    homeworldz::scene::update_linked_world_transform(child, root);
    if (!near(child.position.x, 23.0) || !near(child.position.y, 30.0) ||
        !near(child.position.z, 42.0))
        return 1;
    child.scale = {1.0, 2.0, 3.0};
    homeworldz::scene::scale_linked_child(child, {2.0, 0.5, 3.0});
    if (!near(child.local_position.x, 0.0) || !near(child.local_position.y, -1.5) ||
        !near(child.local_position.z, 6.0) || !near(child.scale.x, 2.0) ||
        !near(child.scale.y, 1.0) || !near(child.scale.z, 9.0))
        return 1;
    homeworldz::scene::Scene scene;
    const auto id = scene.create("moving object", {1.0, 2.0, 3.0}, {4.0, -2.0, 1.0});
    if (scene.size() != 1 || scene.revision() != 1) return 1;

    homeworldz::simulation::FixedStepLoop loop(scene, 0.02, 4);
    if (loop.advance(0.01) != 0 || !near(loop.interpolation_alpha(), 0.5)) return 1;
    if (loop.advance(0.03) != 2) return 1;
    const auto* entity = scene.find(id);
    if (entity == nullptr || !near(entity->position.x, 1.16) ||
        !near(entity->position.y, 1.92) || !near(entity->position.z, 3.04)) return 1;
    if (scene.simulation_steps() != 2 || scene.revision() != 3) return 1;

    if (loop.advance(10.0) != 4) return 1;
    if (scene.simulation_steps() != 6 || loop.interpolation_alpha() >= 1.0) return 1;
    if (!scene.remove(id) || scene.remove(id) || scene.size() != 0) return 1;

    scene.restore(42, std::vector<homeworldz::scene::Entity>{
        {7, "restored", {9.0, 8.0, 7.0}, {1.0, 2.0, 3.0}}});
    const auto* restored = scene.find(7);
    if (scene.size() != 1 || scene.revision() != 42 || scene.simulation_steps() != 0 ||
        restored == nullptr || restored->name != "restored" || !near(restored->position.y, 8.0)) return 1;
    if (scene.create("after restore") != 8) return 1;

    const auto rejects_restore = [](std::vector<homeworldz::scene::Entity> entities) {
        homeworldz::scene::Scene invalid;
        try {
            invalid.restore(1, std::move(entities));
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    homeworldz::scene::Entity orphan;
    orphan.id = 1;
    orphan.parent_id = 99;
    if (!rejects_restore({orphan})) return 1;
    homeworldz::scene::Entity nested_root;
    nested_root.id = 1;
    homeworldz::scene::Entity nested_child;
    nested_child.id = 2;
    nested_child.parent_id = 1;
    homeworldz::scene::Entity nested_grandchild;
    nested_grandchild.id = 3;
    nested_grandchild.parent_id = 2;
    if (!rejects_restore({nested_root, nested_child, nested_grandchild})) return 1;

    homeworldz::scene::Entity permissions;
    permissions.owner_id = "owner";
    if (!homeworldz::scene::apply_permission_update(
            permissions, "owner", homeworldz::scene::permission_field_everyone, true,
            homeworldz::scene::permission_move) ||
        permissions.everyone_permissions != homeworldz::scene::permission_move)
        return 1;
    if (homeworldz::scene::apply_permission_update(
            permissions, "intruder", homeworldz::scene::permission_field_everyone, true,
            homeworldz::scene::permission_copy) ||
        homeworldz::scene::apply_permission_update(
            permissions, "owner", homeworldz::scene::permission_field_base, false,
            homeworldz::scene::permission_modify))
        return 1;
    if (!homeworldz::scene::apply_permission_update(
            permissions, "owner", homeworldz::scene::permission_field_next_owner, false,
            homeworldz::scene::permission_copy) ||
        (permissions.next_owner_permissions & homeworldz::scene::permission_copy) != 0 ||
        (permissions.next_owner_permissions & homeworldz::scene::permission_transfer) == 0 ||
        (permissions.next_owner_permissions & homeworldz::scene::permission_move) == 0)
        return 1;
    if (!homeworldz::scene::apply_permission_update(
            permissions, "owner", homeworldz::scene::permission_field_owner, false,
            homeworldz::scene::permission_modify | homeworldz::scene::permission_move) ||
        (permissions.owner_permissions & homeworldz::scene::permission_move) == 0 ||
        (permissions.owner_permissions & homeworldz::scene::permission_modify) != 0)
        return 1;
    return 0;
}
