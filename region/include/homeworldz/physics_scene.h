#pragma once

#include "homeworldz/physics.h"
#include "homeworldz/scene.h"

#include <cstddef>
#include <unordered_map>

namespace homeworldz::physics {

class StaticSceneMirror {
public:
    explicit StaticSceneMirror(World& world) : world_(world) {}

    bool synchronize(const scene::Entity& entity);
    void synchronize(const scene::Scene& scene);
    bool remove(scene::EntityId entity_id);
    BodyId body_id(scene::EntityId entity_id) const;
    std::size_t size() const { return bodies_.size(); }

private:
    World& world_;
    std::unordered_map<scene::EntityId, BodyId> bodies_;
};

} // namespace homeworldz::physics
