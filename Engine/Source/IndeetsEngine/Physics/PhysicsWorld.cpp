#include "IndeetsEngine/Physics/PhysicsWorld.h"

#include "IndeetsEngine/Core/Log.h"
#include "IndeetsEngine/Scene/Scene.h"

// Jolt.h must be included first.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <mutex>
#include <thread>

namespace ie {
namespace {

namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCount = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr uint32_t kCount = 2;
} // namespace BroadPhaseLayers

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == Layers::kStatic ? BroadPhaseLayers::kStatic : BroadPhaseLayers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == BroadPhaseLayers::kStatic ? "Static" : "Moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
    {
        return layer == Layers::kMoving || bp == BroadPhaseLayers::kMoving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

void InitJoltOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

JPH::Vec3 ToJolt(const glm::vec3& v) { return {v.x, v.y, v.z}; }
JPH::Quat ToJolt(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }
glm::vec3 ToGlm(const JPH::Vec3& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
glm::quat ToGlm(const JPH::Quat& q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

JPH::RefConst<JPH::Shape> BuildShape(const ColliderComponent& c, const glm::vec3& scale)
{
    const glm::vec3 s = glm::max(glm::abs(scale), glm::vec3(0.001f));
    JPH::Ref<JPH::ShapeSettings> settings;
    switch (c.shape) {
    case ColliderShape::Box: {
        glm::vec3 half = glm::max(c.size * s * 0.5f, glm::vec3(0.005f));
        float convex = std::min(0.05f, std::min({half.x, half.y, half.z}) * 0.5f);
        settings = new JPH::BoxShapeSettings(ToJolt(half), convex);
        break;
    }
    case ColliderShape::Sphere:
        settings = new JPH::SphereShapeSettings(std::max(c.radius * std::max({s.x, s.y, s.z}), 0.005f));
        break;
    case ColliderShape::Capsule: {
        float r = std::max(c.radius * std::max(s.x, s.z), 0.005f);
        float halfCylinder = c.height * s.y * 0.5f - r;
        if (halfCylinder > 0.001f)
            settings = new JPH::CapsuleShapeSettings(halfCylinder, r);
        else
            settings = new JPH::SphereShapeSettings(r);
        break;
    }
    }
    if (glm::dot(c.center, c.center) > 0.0f)
        settings = new JPH::RotatedTranslatedShapeSettings(ToJolt(c.center * s), JPH::Quat::sIdentity(), settings);

    JPH::ShapeSettings::ShapeResult result = settings->Create();
    if (result.HasError()) {
        Log::Error("Physics shape error: {}", result.GetError().c_str());
        return nullptr;
    }
    return result.Get();
}

} // namespace

struct PhysicsWorld::Impl {
    BroadPhaseLayerMap broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> system;

    struct BodyLink {
        JPH::BodyID body;
        JPH::EMotionType motion;
    };
    std::unordered_map<EntityID, BodyLink> bodies;
    std::unordered_map<uint32_t, EntityID> entityByBody; // BodyID index+sequence -> entity
};

PhysicsWorld::PhysicsWorld() : m_Impl(std::make_unique<Impl>())
{
    InitJoltOnce();
    m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    m_Impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                                   threads);
    m_Impl->system = std::make_unique<JPH::PhysicsSystem>();
    m_Impl->system->Init(16384, 0, 16384, 16384, m_Impl->broadPhaseLayers, m_Impl->objectVsBroadPhase,
                         m_Impl->objectPairs);
}

PhysicsWorld::~PhysicsWorld()
{
    Stop();
}

void PhysicsWorld::Start(Scene& scene)
{
    Stop();
    JPH::PhysicsSystem& system = *m_Impl->system;
    system.SetGravity(ToJolt(scene.settings.gravity));
    scene.UpdateWorldTransforms();

    for (const auto& entity : scene.Entities())
        AddEntity(*entity);
    system.OptimizeBroadPhase();
    m_Accumulator = 0.0f;
    m_Running = true;
}

void PhysicsWorld::AddEntity(const Entity& entity)
{
    JPH::BodyInterface& bodies = m_Impl->system->GetBodyInterface();
    RemoveEntity(entity.id);
    if (!entity.active || !entity.collider)
        return;
    const ColliderComponent& collider = *entity.collider;
    glm::vec3 worldPosition, worldScale;
    glm::quat worldRotation;
    DecomposeWorld(entity.world, worldPosition, worldRotation, worldScale);
    JPH::RefConst<JPH::Shape> shape = BuildShape(collider, worldScale);
    if (!shape)
        return;

    JPH::EMotionType motion = JPH::EMotionType::Static;
    if (entity.rigidbody)
        motion = entity.rigidbody->isKinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = motion == JPH::EMotionType::Static ? Layers::kStatic : Layers::kMoving;

    JPH::BodyCreationSettings settings(shape, JPH::RVec3(ToJolt(worldPosition)), ToJolt(worldRotation), motion, layer);
    settings.mFriction = collider.friction;
    settings.mRestitution = collider.bounciness;
    settings.mIsSensor = collider.isTrigger;
    if (const auto& rb = entity.rigidbody) {
        settings.mLinearDamping = rb->linearDamping;
        settings.mAngularDamping = rb->angularDamping;
        settings.mGravityFactor = rb->useGravity ? 1.0f : 0.0f;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(rb->mass, 0.001f);
        settings.mAllowSleeping = true;
    }

    JPH::BodyID id = bodies.CreateAndAddBody(
        settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        Log::Warn("Physics: too many bodies, '{}' was skipped", entity.name);
        return;
    }
    m_Impl->bodies[entity.id] = {id, motion};
    m_Impl->entityByBody[id.GetIndexAndSequenceNumber()] = entity.id;
}

void PhysicsWorld::RemoveEntity(EntityID entity)
{
    auto it = m_Impl->bodies.find(entity);
    if (it == m_Impl->bodies.end())
        return;
    JPH::BodyInterface& bodies = m_Impl->system->GetBodyInterface();
    m_Impl->entityByBody.erase(it->second.body.GetIndexAndSequenceNumber());
    bodies.RemoveBody(it->second.body);
    bodies.DestroyBody(it->second.body);
    m_Impl->bodies.erase(it);
}

bool PhysicsWorld::HasBody(EntityID entity) const { return m_Impl->bodies.contains(entity); }

void PhysicsWorld::Update(Scene& scene, float deltaTime, const std::function<void()>& beforeStep)
{
    if (!m_Running)
        return;
    m_Accumulator = std::min(m_Accumulator + deltaTime, kFixedTimeStep * 5.0f); // avoid spiral of death
    while (m_Accumulator >= kFixedTimeStep) {
        if (beforeStep) {
            beforeStep();
            scene.UpdateWorldTransforms();
        }
        FixedStep(scene);
        m_Accumulator -= kFixedTimeStep;
    }
}

void PhysicsWorld::StepOnce(Scene& scene)
{
    if (m_Running)
        FixedStep(scene);
}

void PhysicsWorld::FixedStep(Scene& scene)
{
    JPH::BodyInterface& bodies = m_Impl->system->GetBodyInterface();

    // Kinematic bodies follow their transforms.
    for (auto& [entityId, link] : m_Impl->bodies) {
        if (link.motion != JPH::EMotionType::Kinematic)
            continue;
        if (const Entity* e = scene.Get(entityId)) {
            glm::vec3 p, s;
            glm::quat r;
            DecomposeWorld(e->world, p, r, s);
            bodies.MoveKinematic(link.body, JPH::RVec3(ToJolt(p)), ToJolt(r), kFixedTimeStep);
        }
    }

    m_Impl->system->Update(kFixedTimeStep, 1, m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());

    // Dynamic bodies drive their transforms.
    for (auto& [entityId, link] : m_Impl->bodies) {
        if (link.motion != JPH::EMotionType::Dynamic)
            continue;
        Entity* e = scene.Get(entityId);
        if (!e)
            continue;
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(link.body, position, rotation);
        if (e->parent == 0) {
            e->transform.position = ToGlm(JPH::Vec3(position));
            e->transform.rotation = ToGlm(rotation);
            e->world = e->transform.Matrix();
        } else {
            // Child bodies: convert the simulated world pose back into the parent's space.
            glm::vec3 p, scale;
            glm::quat r;
            DecomposeWorld(e->world, p, r, scale);
            glm::mat4 world = glm::translate(glm::mat4(1.0f), ToGlm(JPH::Vec3(position))) *
                              glm::mat4_cast(ToGlm(rotation)) * glm::scale(glm::mat4(1.0f), scale);
            scene.SetWorldMatrix(*e, world);
        }
    }
    scene.UpdateWorldTransforms();
}

void PhysicsWorld::Stop()
{
    if (!m_Impl->system)
        return;
    JPH::BodyInterface& bodies = m_Impl->system->GetBodyInterface();
    for (auto& [entityId, link] : m_Impl->bodies) {
        bodies.RemoveBody(link.body);
        bodies.DestroyBody(link.body);
    }
    m_Impl->bodies.clear();
    m_Impl->entityByBody.clear();
    m_Running = false;
}

void PhysicsWorld::SetLinearVelocity(EntityID entity, const glm::vec3& velocity)
{
    if (auto it = m_Impl->bodies.find(entity); it != m_Impl->bodies.end())
        m_Impl->system->GetBodyInterface().SetLinearVelocity(it->second.body, ToJolt(velocity));
}

glm::vec3 PhysicsWorld::GetLinearVelocity(EntityID entity) const
{
    if (auto it = m_Impl->bodies.find(entity); it != m_Impl->bodies.end())
        return ToGlm(m_Impl->system->GetBodyInterface().GetLinearVelocity(it->second.body));
    return glm::vec3(0.0f);
}

void PhysicsWorld::SetAngularVelocity(EntityID entity, const glm::vec3& velocity)
{
    if (auto it = m_Impl->bodies.find(entity); it != m_Impl->bodies.end())
        m_Impl->system->GetBodyInterface().SetAngularVelocity(it->second.body, ToJolt(velocity));
}

glm::vec3 PhysicsWorld::GetAngularVelocity(EntityID entity) const
{
    if (auto it = m_Impl->bodies.find(entity); it != m_Impl->bodies.end())
        return ToGlm(m_Impl->system->GetBodyInterface().GetAngularVelocity(it->second.body));
    return glm::vec3(0.0f);
}

void PhysicsWorld::AddForce(EntityID entity, const glm::vec3& force)
{
    if (auto it = m_Impl->bodies.find(entity); it != m_Impl->bodies.end())
        m_Impl->system->GetBodyInterface().AddForce(it->second.body, ToJolt(force));
}

void PhysicsWorld::Teleport(EntityID entity, const glm::vec3& position, const glm::quat& rotation)
{
    auto it = m_Impl->bodies.find(entity);
    if (it == m_Impl->bodies.end() || it->second.motion == JPH::EMotionType::Kinematic)
        return; // kinematic bodies follow their transform every step anyway
    m_Impl->system->GetBodyInterface().SetPositionAndRotation(
        it->second.body, JPH::RVec3(ToJolt(position)), ToJolt(glm::normalize(rotation)),
        it->second.motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
}

void PhysicsWorld::AddImpulse(EntityID entity, const glm::vec3& impulse)
{
    if (auto it = m_Impl->bodies.find(entity); it != m_Impl->bodies.end())
        m_Impl->system->GetBodyInterface().AddImpulse(it->second.body, ToJolt(impulse));
}

size_t PhysicsWorld::BodyCount() const { return m_Impl->bodies.size(); }

std::optional<RaycastHit> PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction,
                                                float maxDistance) const
{
    if (!m_Running)
        return std::nullopt;
    glm::vec3 dir = glm::normalize(direction) * maxDistance;
    JPH::RRayCast ray{JPH::RVec3(ToJolt(origin)), ToJolt(dir)};
    JPH::RayCastResult result;
    if (!m_Impl->system->GetNarrowPhaseQuery().CastRay(ray, result))
        return std::nullopt;

    RaycastHit hit;
    auto it = m_Impl->entityByBody.find(result.mBodyID.GetIndexAndSequenceNumber());
    hit.entity = it != m_Impl->entityByBody.end() ? it->second : 0;
    hit.distance = result.mFraction * maxDistance;
    hit.point = origin + glm::normalize(direction) * hit.distance;
    JPH::BodyLockRead lock(m_Impl->system->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
        hit.normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
    return hit;
}

} // namespace ie
