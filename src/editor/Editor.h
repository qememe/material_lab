#pragma once
#include "physics/PhysicsWorld.h"
#include <optional>
namespace lab {
enum class Tool { Brush, Line, Eraser, Select, Anchor, Heat, Cool };
enum class EditorLayer { Objects, World };
class Editor {
public:
    Tool tool{Tool::Brush};
    EditorLayer layer{EditorLayer::Objects};
    FreezeMode anchorMode{FreezeMode::Absolute};
    MaterialType selectedMaterial{MaterialType::Steel};
    float radius{2};
    float paintTemperature{20}, heaterTemperature{1800};
    std::vector<ParticleId> selection;
    Vec2 launchPoint{};
    float launchSpeed{65}, launchAngle{};
    void beginStroke(PhysicsWorld& world,Vec2 at);
    void continueStroke(PhysicsWorld& world,Vec2 at);
    void endStroke(PhysicsWorld& world,Vec2 at);
    void cancelStroke(PhysicsWorld& world);
    bool undo(PhysicsWorld& world);
    bool redo(PhysicsWorld& world);
    void clearHistory();
    bool drawing() const { return drawing_; }
    Vec2 strokeStart() const { return start_; }
    std::optional<ParticleId> pick(const PhysicsWorld& world,Vec2 at) const;
    Vec2 launchVelocity() const;
    bool insert(PhysicsWorld& world,const PhysicsWorld& construction,Vec2 at,std::string& message);
private:
    struct Snapshot { PhysicsWorld world; std::vector<ParticleId> selection; Vec2 point; };
    std::vector<Snapshot> undo_,redo_;
    std::optional<Snapshot> beforeStroke_;
    std::unordered_map<std::uint64_t,ParticleId> occupied_;
    bool drawing_{};
    Vec2 start_{},last_{};
    void stamp(PhysicsWorld& world,Vec2 at,bool erase);
    void segment(PhysicsWorld& world,Vec2 from,Vec2 to,bool erase);
    void remember(PhysicsWorld& world);
    void rebuildOccupancy(const PhysicsWorld& world);
};
}
