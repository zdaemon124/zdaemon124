#include "IndeetsEngine/Scene/Primitives.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

#include <utility>

namespace ie {
namespace {

// Front faces are clockwise on screen (Unity convention). Generators produce
// triangles with cross(b - a, c - a) pointing along the surface normal; this
// makes sure that holds for every triangle.
void OrientTriangles(MeshData& mesh)
{
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[i]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::dot(faceNormal, a.normal + b.normal + c.normal) < 0.0f)
            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
    }
}

void AddQuad(MeshData& mesh, glm::vec3 center, glm::vec3 u, glm::vec3 v, glm::vec3 normal)
{
    auto base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({center - u - v, normal, {0.0f, 1.0f}});
    mesh.vertices.push_back({center + u - v, normal, {1.0f, 1.0f}});
    mesh.vertices.push_back({center + u + v, normal, {1.0f, 0.0f}});
    mesh.vertices.push_back({center - u + v, normal, {0.0f, 0.0f}});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// Connects rings of (sectors + 1) vertices each, starting at vertex `first`.
void StitchRings(MeshData& mesh, uint32_t first, uint32_t rings, uint32_t sectors)
{
    for (uint32_t r = 0; r + 1 < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            uint32_t a = first + r * (sectors + 1) + s;
            uint32_t b = a + sectors + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
}

void AddCap(MeshData& mesh, float radius, float y, glm::vec3 normal, uint32_t sectors)
{
    auto center = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0.0f, y, 0.0f}, normal, {0.5f, 0.5f}});
    for (uint32_t s = 0; s <= sectors; ++s) {
        float theta = glm::two_pi<float>() * float(s) / float(sectors);
        float cx = std::cos(theta), cz = std::sin(theta);
        mesh.vertices.push_back({{cx * radius, y, cz * radius}, normal, {0.5f + cx * 0.5f, 0.5f - cz * 0.5f}});
    }
    for (uint32_t s = 0; s < sectors; ++s)
        mesh.indices.insert(mesh.indices.end(), {center, center + 1 + s, center + 2 + s});
}

} // namespace

const char* PrimitiveName(PrimitiveType type)
{
    switch (type) {
    case PrimitiveType::Cube: return "Cube";
    case PrimitiveType::Sphere: return "Sphere";
    case PrimitiveType::Capsule: return "Capsule";
    case PrimitiveType::Cylinder: return "Cylinder";
    case PrimitiveType::Plane: return "Plane";
    case PrimitiveType::Quad: return "Quad";
    default: return "Unknown";
    }
}

namespace Primitives {

MeshData Cube(float size)
{
    MeshData mesh;
    float h = size * 0.5f;
    const glm::vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    AddQuad(mesh, X * h, -Z * h, Y * h, X);
    AddQuad(mesh, -X * h, Z * h, Y * h, -X);
    AddQuad(mesh, Y * h, X * h, -Z * h, Y);
    AddQuad(mesh, -Y * h, X * h, Z * h, -Y);
    AddQuad(mesh, Z * h, X * h, Y * h, Z);
    AddQuad(mesh, -Z * h, -X * h, Y * h, -Z);
    OrientTriangles(mesh);
    return mesh;
}

MeshData Sphere(float radius, uint32_t sectors, uint32_t stacks)
{
    MeshData mesh;
    for (uint32_t i = 0; i <= stacks; ++i) {
        float phi = glm::pi<float>() * float(i) / float(stacks); // 0 at top
        for (uint32_t s = 0; s <= sectors; ++s) {
            float theta = glm::two_pi<float>() * float(s) / float(sectors);
            glm::vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            mesh.vertices.push_back({n * radius, n, {float(s) / float(sectors), float(i) / float(stacks)}});
        }
    }
    StitchRings(mesh, 0, stacks + 1, sectors);
    OrientTriangles(mesh);
    return mesh;
}

MeshData Capsule(float radius, float height, uint32_t sectors, uint32_t stacks)
{
    MeshData mesh;
    float halfCylinder = std::max(height * 0.5f - radius, 0.0f);
    uint32_t half = stacks / 2;
    uint32_t rings = 0;
    // Top hemisphere rings (shifted up) followed by bottom hemisphere rings (shifted down);
    // the gap between the two equator rings forms the cylinder part.
    for (int hemisphere = 0; hemisphere < 2; ++hemisphere) {
        for (uint32_t i = 0; i <= half; ++i) {
            float phi = glm::half_pi<float>() * (float(i) / float(half) + float(hemisphere));
            float offset = hemisphere == 0 ? halfCylinder : -halfCylinder;
            for (uint32_t s = 0; s <= sectors; ++s) {
                float theta = glm::two_pi<float>() * float(s) / float(sectors);
                glm::vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
                glm::vec3 p = n * radius + glm::vec3(0.0f, offset, 0.0f);
                mesh.vertices.push_back({p, n, {float(s) / float(sectors), 0.5f - p.y / height}});
            }
            ++rings;
        }
    }
    StitchRings(mesh, 0, rings, sectors);
    OrientTriangles(mesh);
    return mesh;
}

MeshData Cylinder(float radius, float height, uint32_t sectors)
{
    MeshData mesh;
    float h = height * 0.5f;
    for (int ring = 0; ring < 2; ++ring) {
        float y = ring == 0 ? h : -h;
        for (uint32_t s = 0; s <= sectors; ++s) {
            float theta = glm::two_pi<float>() * float(s) / float(sectors);
            glm::vec3 n{std::cos(theta), 0.0f, std::sin(theta)};
            mesh.vertices.push_back({{n.x * radius, y, n.z * radius}, n, {float(s) / float(sectors), float(ring)}});
        }
    }
    StitchRings(mesh, 0, 2, sectors);
    AddCap(mesh, radius, h, {0, 1, 0}, sectors);
    AddCap(mesh, radius, -h, {0, -1, 0}, sectors);
    OrientTriangles(mesh);
    return mesh;
}

MeshData Plane(float size, uint32_t subdivisions)
{
    MeshData mesh;
    float step = size / float(subdivisions);
    float start = -size * 0.5f;
    for (uint32_t z = 0; z <= subdivisions; ++z)
        for (uint32_t x = 0; x <= subdivisions; ++x)
            mesh.vertices.push_back({{start + float(x) * step, 0.0f, start + float(z) * step},
                                     {0.0f, 1.0f, 0.0f},
                                     {float(x) / float(subdivisions), 1.0f - float(z) / float(subdivisions)}});
    StitchRings(mesh, 0, subdivisions + 1, subdivisions);
    OrientTriangles(mesh);
    return mesh;
}

MeshData Quad(float size)
{
    MeshData mesh;
    float h = size * 0.5f;
    AddQuad(mesh, {0, 0, 0}, {-h, 0, 0}, {0, h, 0}, {0, 0, -1}); // faces -Z, like Unity
    OrientTriangles(mesh);
    return mesh;
}

MeshData Create(PrimitiveType type)
{
    switch (type) {
    case PrimitiveType::Cube: return Cube();
    case PrimitiveType::Sphere: return Sphere();
    case PrimitiveType::Capsule: return Capsule();
    case PrimitiveType::Cylinder: return Cylinder();
    case PrimitiveType::Plane: return Plane();
    case PrimitiveType::Quad: return Quad();
    default: return {};
    }
}

} // namespace Primitives
} // namespace ie
