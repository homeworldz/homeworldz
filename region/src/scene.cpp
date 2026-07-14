#include "homeworldz/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace homeworldz::scene {

std::optional<RayIntersection> intersect_box(
    Vector3 ray_start, Vector3 ray_end, Vector3 center, Vector3 scale) {
    const std::array start{ray_start.x, ray_start.y, ray_start.z};
    const std::array direction{
        ray_end.x - ray_start.x, ray_end.y - ray_start.y, ray_end.z - ray_start.z};
    const std::array minimum{
        center.x - scale.x * 0.5, center.y - scale.y * 0.5, center.z - scale.z * 0.5};
    const std::array maximum{
        center.x + scale.x * 0.5, center.y + scale.y * 0.5, center.z + scale.z * 0.5};
    double near_time = 0.0;
    double far_time = 1.0;
    std::size_t near_axis = 0;
    double near_sign = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (scale.x <= 0.0 || scale.y <= 0.0 || scale.z <= 0.0) return std::nullopt;
        if (std::abs(direction[axis]) < 1e-12) {
            if (start[axis] < minimum[axis] || start[axis] > maximum[axis]) return std::nullopt;
            continue;
        }
        double first = (minimum[axis] - start[axis]) / direction[axis];
        double second = (maximum[axis] - start[axis]) / direction[axis];
        double sign = -1.0;
        if (first > second) {
            std::swap(first, second);
            sign = 1.0;
        }
        if (first > near_time) {
            near_time = first;
            near_axis = axis;
            near_sign = sign;
        }
        far_time = std::min(far_time, second);
        if (near_time > far_time) return std::nullopt;
    }
    if (near_sign == 0.0 || near_time < 0.0 || near_time > 1.0) return std::nullopt;
    RayIntersection result;
    result.position = {ray_start.x + direction[0] * near_time,
                       ray_start.y + direction[1] * near_time,
                       ray_start.z + direction[2] * near_time};
    if (near_axis == 0) result.normal.x = near_sign;
    if (near_axis == 1) result.normal.y = near_sign;
    if (near_axis == 2) result.normal.z = near_sign;
    return result;
}

EntityId Scene::create(std::string name, Vector3 position, Vector3 velocity) {
    const auto id = next_id_++;
    entities_.emplace(id, Entity{id, std::move(name), position, velocity});
    ++revision_;
    return id;
}

bool Scene::remove(EntityId id) {
    if (entities_.erase(id) == 0) return false;
    ++revision_;
    return true;
}

Entity* Scene::find(EntityId id) {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
}

const Entity* Scene::find(EntityId id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
}

void Scene::step(double seconds) {
    for (auto& [id, entity] : entities_) {
        static_cast<void>(id);
        entity.position.x += entity.velocity.x * seconds;
        entity.position.y += entity.velocity.y * seconds;
        entity.position.z += entity.velocity.z * seconds;
    }
    ++simulation_steps_;
    ++revision_;
}

void Scene::restore(std::uint64_t revision, std::vector<Entity> entities) {
    std::unordered_map<EntityId, Entity> restored;
    restored.reserve(entities.size());
    EntityId next_id = 1;
    for (auto& entity : entities) {
        if (entity.id == 0 || entity.id == std::numeric_limits<EntityId>::max()) {
            throw std::invalid_argument("restored entity ID is outside the supported range");
        }
        next_id = std::max(next_id, entity.id + 1);
        const auto [position, inserted] = restored.emplace(entity.id, std::move(entity));
        static_cast<void>(position);
        if (!inserted) throw std::invalid_argument("restored scene contains duplicate entity IDs");
    }
    entities_ = std::move(restored);
    next_id_ = next_id;
    revision_ = revision;
    simulation_steps_ = 0;
}

} // namespace homeworldz::scene
