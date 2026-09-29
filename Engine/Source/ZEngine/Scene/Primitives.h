#pragma once

#include "ZEngine/Renderer/Mesh.h"

namespace ze {

// Same set and default sizes as Unity's GameObject.CreatePrimitive.
enum class PrimitiveType { Cube, Sphere, Capsule, Cylinder, Plane, Quad, Count };

const char* PrimitiveName(PrimitiveType type);

namespace Primitives {
MeshData Cube(float size = 1.0f);
MeshData Sphere(float radius = 0.5f, uint32_t sectors = 48, uint32_t stacks = 24);
MeshData Capsule(float radius = 0.5f, float height = 2.0f, uint32_t sectors = 48, uint32_t stacks = 24);
MeshData Cylinder(float radius = 0.5f, float height = 2.0f, uint32_t sectors = 48);
MeshData Plane(float size = 10.0f, uint32_t subdivisions = 10);
MeshData Quad(float size = 1.0f);
MeshData Create(PrimitiveType type);
} // namespace Primitives

} // namespace ze
