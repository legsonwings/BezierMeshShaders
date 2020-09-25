
#include "stdafx.h"
#include "BezierMaths.h"

#include <utility>
#include <iterator>

using namespace std;
using namespace  DirectX::SimpleMath;

TriangularBezierPatch TriangularBezierPatch::Rotate(Vector3 const& axis, float const angle) const
{
    TriangularBezierPatch result;
    auto const nPoints = _countof(this->controlPoints);
    for (int i = 0; i < nPoints; ++i)
    {
        Vector3::Transform(this->controlPoints[i], Quaternion::CreateFromAxisAngle(axis, angle), result.controlPoints[i]);
    }

    return result;
}

Vertex TriangularBezierPatch::EvaluateVertex(Vector3 const& uvw, TriangularBezierPatch const& patch)
{
    Vector3 p010 = patch.controlPoints[1] * uvw.x + patch.controlPoints[0] * uvw.y + patch.controlPoints[2] * uvw.z;
    Vector3 p100 = patch.controlPoints[3] * uvw.x + patch.controlPoints[1] * uvw.y + patch.controlPoints[4] * uvw.z;
    Vector3 p001 = patch.controlPoints[4] * uvw.x + patch.controlPoints[2] * uvw.y + patch.controlPoints[5] * uvw.z;

    auto tangent = p100 - p010;
    tangent.Normalize();
    auto biTangent = p001 - p010;
    biTangent.Normalize();

    auto cross = tangent.Cross(biTangent);
    cross.Normalize();
    return { { p100 * uvw.x + p010 * uvw.y + p001 * uvw.z }, cross };
}

vector<Triangle> TriangularBezierPatch::TessellatePatch(TriangularBezierPatch const& bPatch)
{
    auto lerp = [](float a, float b, float t)
    {
        return a * (1 - t) + b * t;
    };

    auto lerpV = [&lerp](Vector3 const& a, Vector3 const& b, float t) -> Vector3
    {
        return { lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t) };
    };

    static constexpr int numLayers = 32;
    static constexpr int numTotalTris = numLayers * numLayers;
    static constexpr float step = 1.f / numLayers;

    std::vector<Triangle> result;

    // Reserve space for all triangles
    // Use sum of arithemetic series to detrmine the total size
    // Turns out the sum of series = n*n
    result.reserve(numTotalTris);

    // Add a minor tolerance to account for FP inaccuracies
    float end = 0.f + step - 0.0001f;
    for (int layer = 0; layer < numLayers; ++layer)
    {
        float topV, botV;
        topV = 1.f - layer * step;
        botV = 1.f - ((layer + 1) * step);
        Vector3 topLeft = { 1.f - topV, topV, 0.f };
        Vector3 topRight = { 0.f, topV, 1.f - topV };

        Vector3 botLeft = { 1.f - botV, botV, 0.f };
        Vector3 botRight = { 0.f, botV, 1.f - botV };

        // Compute the first edge
        Vector3 topVert = lerpV(topLeft, topRight, 0.f);
        Vector3 botLeftVert = lerpV(botLeft, botRight, 0.f);

        auto lastEdge = make_pair<Vertex>(EvaluateVertex(botLeftVert, bPatch), EvaluateVertex(topVert, bPatch));
        int const numLayerTris = (2 * layer + 1);

        // Compute the triangle strip for each layer
        for (int triIdx = 0; triIdx < numLayerTris; ++triIdx)
        {
            if (triIdx % 2 == 0)
            {
                // Skip inverted triangles when calculating interpolation t which do not contribute in change of horizontal(uw) parameter span
                // In total there will be floor(nTris / 2) such triangles
                Vector3 botRightVert = lerpV(botLeft, botRight, (float(triIdx - (triIdx / 2)) + 1.f) / (float(layer) + 1.f));
                result.push_back({ lastEdge.first, lastEdge.second, EvaluateVertex(botRightVert, bPatch) });
                lastEdge = { lastEdge.second, result.back().vertices[2] };
            }
            else
            {
                // Skip inverted triangles when calculating interpolation t which do not contribute in change of horizontal(uw) parameter span
                // In total there will be floor(nTris / 2) such triangles
                Vector3 topRightVert = lerpV(topLeft, topRight, (float(triIdx - (triIdx / 2))) / float(layer));
                result.push_back({ lastEdge.first, EvaluateVertex(topRightVert, bPatch), lastEdge.second });
                lastEdge = { lastEdge.second, result.back().vertices[1] };
            }
        }
    }

    return result;
}

vector<Triangle> BezierShape::Tessellate() const
{
    vector<Triangle> result;
    for (int i = 0; i < NumPatches; ++i)
    {
        auto tesselatedPatch = TriangularBezierPatch::TessellatePatch(Patches[i]);
        move(tesselatedPatch.begin(), tesselatedPatch.end(), back_inserter(result));
    }

    return result;
}

Vector3 BezierShape::GetCenter() const
{
    return Vector3::Zero;
}
