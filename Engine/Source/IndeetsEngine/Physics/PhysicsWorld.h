#pragma once

#include <glm/glm.hpp>

#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ie {

class Scene;
struct Entity;
using EntityID = uint32_t;

struct RaycastHit {
    EntityID entity = 0;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

enum class ContactEventType : int32_t { Enter = 0, Stay = 1, Exit = 2 };

// A collision or trigger contact between two entities, reported after a physics step (Unity's
// OnCollisionEnter/Stay/Exit and OnTriggerEnter/Stay/Exit). The layout is shared with the
// managed side (IndeetsEngine.Interop.ContactEvent).
struct ContactEvent {
    EntityID a = 0;
    EntityID b = 0;
    ContactEventType type = ContactEventType::Enter;
    int32_t trigger = 0;          // one of the colliders is a trigger
    glm::vec3 point{0.0f};        // world contact point
    glm::vec3 normal{0.0f};       // from a to b
    glm::vec3 relativeVelocity{0.0f}; // velocity of b relative to a
};

// Rigid body simulation (Jolt Physics) driven by Rigidbody/Collider components.
//  - Collider only            -> static body
//  - Collider + Rigidbody      -> dynamic body (or kinematic when isKinematic)
class PhysicsWorld {
public:
    static constexpr float kFixedTimeStep = 1.0f / 60.0f;

    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Creates bodies for the scene (entering play mode).
    void Start(Scene& scene);
    // Adds (or rebuilds) the body for an entity created or changed while running (e.g. a spawned projectile).
    // Uses Entity::world, so it must be up to date.
    void AddEntity(const Entity& entity);
    void RemoveEntity(EntityID entity);
    bool HasBody(EntityID entity) const;
    // Advances the simulation with a fixed time step and writes poses back to the scene.
    // `beforeStep` runs before every fixed step (scripts' FixedUpdate), `afterStep` after it with the
    // contacts that step produced (collision and trigger messages).
    using ContactCallback = std::function<void(const std::vector<ContactEvent>&)>;
    void Update(Scene& scene, float deltaTime, const std::function<void()>& beforeStep = {},
                const ContactCallback& afterStep = {});
    // Runs exactly one fixed step (editor "Step" button).
    void StepOnce(Scene& scene, const ContactCallback& afterStep = {});
    // Destroys all bodies (leaving play mode).
    void Stop();
    bool IsRunning() const { return m_Running; }

    void SetLinearVelocity(EntityID entity, const glm::vec3& velocity);
    glm::vec3 GetLinearVelocity(EntityID entity) const;
    void SetAngularVelocity(EntityID entity, const glm::vec3& velocity);
    glm::vec3 GetAngularVelocity(EntityID entity) const;
    void AddImpulse(EntityID entity, const glm::vec3& impulse);
    // Continuous force, applied over the next step.
    void AddForce(EntityID entity, const glm::vec3& force);
    // Moves a body instantly (a script or the editor set the transform).
    void Teleport(EntityID entity, const glm::vec3& position, const glm::quat& rotation);

    std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;

    size_t BodyCount() const;

private:
    void FixedStep(Scene& scene);
    void CollectContacts();

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
    float m_Accumulator = 0.0f;
    bool m_Running = false;
};

} // namespace ie
