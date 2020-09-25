#pragma once

#include <vector>
#include <cstdint>
#include "SimpleMath.h"

struct Vertex
{
    DirectX::SimpleMath::Vector3 position;
    DirectX::SimpleMath::Vector3 normal;
};

struct Triangle
{
    Vertex vertices[3];
};

struct TriangularBezierPatch
{
    using ControlPoint = DirectX::SimpleMath::Vector3;
    ControlPoint controlPoints[6];

    TriangularBezierPatch Rotate(DirectX::SimpleMath::Vector3 const& axis, float const angle) const;
    
    static std::vector<Triangle> TessellatePatch(TriangularBezierPatch const& bPatch);
    static Vertex EvaluateVertex(DirectX::SimpleMath::Vector3 const& uvw, TriangularBezierPatch const& patch);
};

struct BezierShape
{
    static constexpr int NumPatches = 1;
    TriangularBezierPatch Patches[NumPatches];
    std::vector<Triangle> Tessellate() const;

    DirectX::SimpleMath::Vector3 GetCenter() const;
};