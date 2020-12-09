#pragma once

#include <vector>
#include <cstdint>
#include "SimpleMath.h"
#include <utility>
#include <iterator>

struct Vertex
{
    Vertex() = default;

    constexpr Vertex(DirectX::SimpleMath::Vector3 pos, DirectX::SimpleMath::Vector3 norm)
        :position(pos), normal(norm)
    {}

    DirectX::SimpleMath::Vector3 position;
    DirectX::SimpleMath::Vector3 normal;
};

struct Triangle
{
    Vertex vertices[3];
};

struct Line
{
    DirectX::SimpleMath::Vector3 start;
    DirectX::SimpleMath::Vector3 end;
};

namespace BezierMaths
{
    using ControlPoint = DirectX::SimpleMath::Vector3;
    constexpr int ceil(float value)
    {
        int intVal = static_cast<int>(value);
        if (value == static_cast<float>(intVal))
        {
            return intVal;
        }
        return intVal + (value > 0.f ? 1 : 0);
    }

    template<typename T>
    constexpr T lerp(T a, T b, float t)
    {
        return a * (1 - t) + b * t;
    };

    template<unsigned N>
    struct TriangularIndex
    {
        constexpr TriangularIndex(unsigned _i, unsigned _j, unsigned _k)
            : i(_i), j(_j), k(_k)
        {}

        constexpr unsigned To1D() const
        {
            // N - j gives us row index
            return (N - j) * (N - j + 1) / 2 + k;
        }

        static constexpr unsigned To1D(unsigned j, unsigned k)
        {
            // N - j gives us row index
            return (N - j) * (N - j + 1) / 2 + k;
        }

        static constexpr TriangularIndex From1D(unsigned idx1D)
        {
            // Determine which row the control point belongs to
            // Add a very small tolerance to account for potential FP inaccuracies
            float const temp = (sqrt((static_cast<float>(idx1D) + 1.f) * 8.f + 1.f) - 1.f) / 2.0f - 0.000001f;

            // Convert to 0 based index
            unsigned const row = static_cast<unsigned>(BezierMaths::ceil(temp)) - 1u;

            // This will give the index of starting vertex of the row
            // Number of vertices till layer n(0-based) is n(n+1)/2
            unsigned const rowStartVertex = (row) * (row + 1) / 2;

            unsigned const j = N - row;
            unsigned const k = idx1D - rowStartVertex;
            unsigned const i = N - j - k;

            return TriangularIndex(i, j, k);
        }

        unsigned i, j, k;
    };

    template<unsigned N>
    struct BezierCurve
    {
        constexpr BezierCurve() = default;

        static constexpr unsigned NumControlPoints = N + 1;
        ControlPoint ControlPoints[NumControlPoints] = {};
    };

    template<unsigned N>
    struct BezierTriangle
    {
        constexpr BezierTriangle() = default;

        static constexpr unsigned NumControlPoints = ((N + 1) * (N + 2)) / 2;
        ControlPoint ControlPoints[NumControlPoints] = {};

        constexpr DirectX::SimpleMath::Vector3 operator[](unsigned index) const
        {
            return ControlPoints[index];
        }

        BezierTriangle<N> Rotate(DirectX::SimpleMath::Vector3 const& axis, float const angle) const
        {
            BezierTriangle<N> result;
            for (int i = 0; i < NumControlPoints; ++i)
            {
                DirectX::SimpleMath::Vector3::Transform(this->ControlPoints[i], DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(axis, angle), result.ControlPoints[i]);
            }

            return result;
        }
    };

    template<unsigned N, unsigned M>
    struct BezierShape
    {
        BezierTriangle<N> Patches[M] = {};

        static constexpr unsigned GetDegree() { return N; }
        static constexpr unsigned GetNumPatchs() { return M; }
        DirectX::SimpleMath::Vector3 GetCenter() const { return DirectX::SimpleMath::Vector3::Zero; }
    };

    template<unsigned N, unsigned S>
    struct Decasteljau
    {
        static_assert(N >= 0, "Degree cannot be negative.");
        static_assert(N >= S, "Degree cannot be smaller than subdivision end degree.");

        static constexpr BezierCurve<S> Curve(BezierCurve<N> curve, float t)
        {
            BezierCurve<N - 1u> subCurve;
            for (int i = 0; i < subCurve.NumControlPoints; ++i)
            {
                subCurve.ControlPoints[i] = curve.ControlPoints[i] * (1 - t) + curve.ControlPoints[i + 1] * t;
            }

            return Decasteljau<N - 1u, S>::Curve(subCurve, t);
        }

        static constexpr BezierTriangle<S> Triangle(BezierTriangle<N> patch, DirectX::SimpleMath::Vector3 const& uvw)
        {
            BezierTriangle<N - 1u> subpatch;
            auto const& ControlPoints = patch.ControlPoints;
            for (int i = 0; i < subpatch.NumControlPoints; ++i)
            {
                auto const& idx = TriangularIndex<N - 1u>::From1D(i);
                subpatch.ControlPoints[i] = ControlPoints[TriangularIndex<N>::To1D(idx.j, idx.k)] * uvw.x +
                                            ControlPoints[TriangularIndex<N>::To1D(idx.j + 1, idx.k)] * uvw.y +
                                            ControlPoints[TriangularIndex<N>::To1D(idx.j, idx.k + 1)] * uvw.z;
            }

            return Decasteljau<N - 1u, S>::Triangle(subpatch, uvw);
        }
    };

    template<unsigned N>
    struct Decasteljau<N, N>
    {
        static_assert(N >= 0, "Degree cannot be negative.");
        static constexpr BezierCurve<N> Curve(BezierCurve<N> curve, float t) { return curve; }
        static constexpr BezierTriangle<N> Triangle(BezierTriangle<N> patch, DirectX::SimpleMath::Vector3 const& uvw) { return patch; }
    };

    template<unsigned N>
    static constexpr Vertex Evaluate(BezierCurve<N> const& curve, float t)
    {
        static_assert(N > 1, "This version only works with curves of degree 2 or more.");

        auto const& line = Decasteljau<N, 1>::Curve(curve, t);
        auto const& position = line.ControlPoints[0] * (1 - t) + line.ControlPoints[1] * t;

        DirectX::SimpleMath::Plane curvePlane(curve.ControlPoints[2], curve.ControlPoints[1], curve.ControlPoints[0]);
        auto const& tangent = line.ControlPoints[1] - line.ControlPoints[0];
        auto normal = curvePlane.Normal().Cross(tangent);

        normal.Normalize();
        return { position, normal };
    };

    template<>
    static constexpr Vertex Evaluate<1>(BezierCurve<1> const& curve, float t)
    {
        // Can't calculate normal for a degree 0 curve
        return { Decasteljau<1, 0>::Curve(curve, t).ControlPoints[0], DirectX::SimpleMath::Vector3::Zero };
    };

    template<unsigned N>
    Vertex Evaluate(BezierTriangle<N> const& patch, DirectX::SimpleMath::Vector3 const& uvw)
    {
        auto const& triangle = Decasteljau<N, 1>::Triangle(patch, uvw);
        auto const& vertices = triangle.ControlPoints;

        ControlPoint const& p010 = vertices[TriangularIndex<N>::To1D(1, 0)];
        ControlPoint const& p100 = vertices[TriangularIndex<N>::To1D(0, 0)];
        ControlPoint const& p001 = vertices[TriangularIndex<N>::To1D(0, 1)];

        auto tangent = p100 - p010;
        auto biTangent = p001 - p010;
        tangent.Normalize();
        biTangent.Normalize();

        auto normal = tangent.Cross(biTangent);
        normal.Normalize();
        return { { p100 * uvw.x + p010 * uvw.y + p001 * uvw.z }, normal };
    }

    template<unsigned N>
    std::vector<Line> GetWireFrameControlMesh(BezierTriangle<N> const& patch)
    {
        std::vector<Line> result;

        for (int row = 0; row < N; ++row)
        {
            auto previousRowStart = row * (row + 1) / 2;
            auto nextRowStart = (row + 1) * (row + 2) / 2;

            auto previousRowEnd = previousRowStart + row;
            auto nextRowEnd = nextRowStart + row + 1;

            result.push_back({ patch.ControlPoints[previousRowStart], patch.ControlPoints[nextRowStart] });
            result.push_back({ patch.ControlPoints[previousRowEnd], patch.ControlPoints[nextRowEnd] });

            for (int nextRowPt = previousRowEnd + 1; nextRowPt < previousRowEnd + 1 + row + 1 ; ++nextRowPt)
            {
                // This is the line to next point in this row
                result.push_back({ patch.ControlPoints[nextRowPt], patch.ControlPoints[nextRowPt + 1] });

                // These are lines from the next point to previous row(form inverted traingles)
                if (nextRowPt < previousRowEnd + 1 + row)
                {
                    auto startPtInPrevRow = previousRowStart + nextRowPt - (previousRowEnd + 1);
                    result.push_back({ patch.ControlPoints[nextRowPt + 1], patch.ControlPoints[startPtInPrevRow] });
                    result.push_back({ patch.ControlPoints[nextRowPt + 1], patch.ControlPoints[startPtInPrevRow + 1] });
                }
            }
        }

        return result;
    }

    template<unsigned N>
    std::vector<Triangle> TessellatePatch(BezierTriangle<N> const& patch)
    {
        using Vector3 = DirectX::SimpleMath::Vector3;

        static constexpr int numRows = 16;
        static constexpr int numTotalTris = numRows * numRows;
        static constexpr float step = 1.f / numRows;

        std::vector<Triangle> result;

        // Reserve space for all triangles
        // Use sum of arithemetic series to detrmine the total size
        // Turns out the sum of series = n*n
        result.reserve(numTotalTris);

        // Add a minor tolerance to account for FP inaccuracies
        float end = 0.f + step - 0.0001f;
        for (int row = 0; row < numRows; ++row)
        {
            float topV, botV;
            topV = 1.f - row * step;
            botV = 1.f - ((row + 1) * step);
            Vector3 topLeft = { 1.f - topV, topV, 0.f };
            Vector3 topRight = { 0.f, topV, 1.f - topV };

            Vector3 botLeft = { 1.f - botV, botV, 0.f };
            Vector3 botRight = { 0.f, botV, 1.f - botV };

            // Compute the first edge
            Vector3 topVert = lerp(topLeft, topRight, 0.f);
            Vector3 botLeftVert = lerp(botLeft, botRight, 0.f);

            auto lastEdge = std::make_pair<Vertex>(Evaluate(patch, botLeftVert), Evaluate(patch, topVert));
            int const numRowTris = (2 * row + 1);

            // Compute the triangle strip for each layer
            for (int triIdx = 0; triIdx < numRowTris; ++triIdx)
            {
                static const auto norm = Vector3{ 0.f, 0.f, 1.f };
                if (triIdx % 2 == 0)
                {
                    // Skip intermediate triangles when calculating interpolation t which do not contribute in change of horizontal(uw) parameter span
                    // In total there will be floor(nTris / 2) such triangles
                    Vector3 botRightVert = lerp(botLeft, botRight, (float(triIdx - (triIdx / 2)) + 1.f) / (float(row) + 1.f));
                    result.push_back({ lastEdge.first, lastEdge.second, Evaluate(patch, botRightVert) });
                    lastEdge = { lastEdge.second, result.back().vertices[2] };
                }
                else
                {
                    // Skip intermediate triangles when calculating interpolation t which do not contribute in change of horizontal(uw) parameter span
                    // In total there will be floor(nTris / 2) such triangles
                    Vector3 topRightVert = lerp(topLeft, topRight, (float(triIdx - (triIdx / 2))) / float(row));
                    result.push_back({ lastEdge.first, Evaluate(patch, topRightVert), lastEdge.second });
                    lastEdge = { lastEdge.second, result.back().vertices[1] };
                }
            }
        }

        return result;
    }

    template<unsigned N, unsigned M>
    std::vector<Triangle> TessellateShape(BezierShape<N, M> const& shape)
    {
        std::vector<Triangle> result;
        for (int i = 0; i < M; ++i)
        {
            auto tesselatedPatch = TessellatePatch(shape.Patches[i]);
            std::move(tesselatedPatch.begin(), tesselatedPatch.end(), back_inserter(result));
        }

        return result;
    }

    template<unsigned N>
    constexpr BezierTriangle<N + 1> Elevate(BezierTriangle<N> const& patch)
    {
        BezierTriangle<N + 1> elevatedPatch;
        for (int i = 0; i < elevatedPatch.NumControlPoints; ++i)
        {
            auto const& idx = TriangularIndex<N + 1>::From1D(i);

            // Subtraction will yeild negative indices at times ignore such points
            auto const& term0 = idx.i == 0 ? DirectX::SimpleMath::Vector3::Zero : patch.ControlPoints[TriangularIndex<N>::To1D(idx.j, idx.k)] * idx.i;
            auto const& term1 = idx.j == 0 ? DirectX::SimpleMath::Vector3::Zero : patch.ControlPoints[TriangularIndex<N>::To1D(idx.j - 1, idx.k)] * idx.j;
            auto const& term2 = idx.k == 0 ? DirectX::SimpleMath::Vector3::Zero : patch.ControlPoints[TriangularIndex<N>::To1D(idx.j, idx.k - 1)] * idx.k;

            elevatedPatch.ControlPoints[i] = (term0 + term1 + term2) / (N + 1);
        }

        return elevatedPatch;
    }
}