#include "BezierShared.hlsli"

struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS : POSITION0;
    float3 Normal : NORMAL0;
};

struct PatchVertex
{
    float3 Position;
    float3 Normal;
};

ConstantBuffer<Constants> Globals : register(b0);
StructuredBuffer<float3> Patches : register(t0);

PatchVertex EvaluateVertex(const float3 uvw, uint patchIdx)
{
    static const uint NumControPointsPerPatch = 6;
    const uint patchStartIdx = patchIdx * NumControPointsPerPatch;
        
    // Use Decasteljau's to evaluate vertex
    float3 p010 = Patches[patchStartIdx + 1] * uvw.x + Patches[patchStartIdx] * uvw.y + Patches[patchStartIdx + 2] * uvw.z;
    float3 p100 = Patches[patchStartIdx + 3] * uvw.x + Patches[patchStartIdx + 1] * uvw.y + Patches[patchStartIdx + 4] * uvw.z;
    float3 p001 = Patches[patchStartIdx + 4] * uvw.x + Patches[patchStartIdx + 2] * uvw.y + Patches[patchStartIdx + 5] * uvw.z;

    float3 tangent = normalize(p100 - p010);
    float3 biTangent = normalize(p001 - p010);

    PatchVertex outVert;

    outVert.Position = p100 * uvw.x + p010 * uvw.y + p001 * uvw.z;
    outVert.Normal = normalize(cross(tangent, biTangent));
    
    return outVert;
}
 
struct Triangle
{
    VertexOut v0;
    VertexOut v1;
    VertexOut v2;
};

VertexOut GetVertAttribute(PatchVertex vertex)
{
    VertexOut outVert;
    
    outVert.PositionHS = mul(float4(vertex.Position, 1), Globals.WorldViewProj);
    outVert.PositionVS = mul(float4(vertex.Position, 1), Globals.WorldView).xyz;
    outVert.Normal = mul(float4(vertex.Normal, 0), Globals.World).xyz;
    
    return outVert;
}

uint2 GetRowAndRelativeTriIndices(uint patchTriIdx)
{
    // Determine which row the passed triangle index(relative to patch) belongs to
    // floor(sqrt(idx)) gives the row
    uint row = sqrt(patchTriIdx);
    
    // This will give the index of starting triangle of the row
    // Number of tris till row n is n * n
    uint rowStartPatchTriIdx = row * row;
    uint rowTriIdx = patchTriIdx - rowStartPatchTriIdx;
    
    return uint2(row, rowTriIdx);
}

Triangle GetTriangle(uint patchIdx, uint patchTriIdx)
{
    Triangle ret;

    float step = 1.f / Globals.NumTesselationRowsPerPatch;
    
    uint2 rowAndRelTriIdx = GetRowAndRelativeTriIndices(patchTriIdx);
    uint row = rowAndRelTriIdx.x;
    uint rowTriIdx = rowAndRelTriIdx.y;
    
    float topV = 1.f - row * step;
    float botV = 1.f - ((row + 1) * step);

    float3 topLeft = { 1.f - topV, topV, 0.f };
    float3 topRight = { 0.f, topV, 1.f - topV };

    float3 botLeft = { 1.f - botV, botV, 0.f };
    float3 botRight = { 0.f, botV, 1.f - botV };

    // Note : This branch is bad for performance due to divergence and thread warp size
    // Note : Some divisions in lerp "t" calculations are integer on purpose
    // Every second triangle in a row is inverted
    if (rowTriIdx % 2 == 0)
    {
        // Skip inverted triangles when calculating interpolation t which do not contribute in change of horizontal(uw) parameter span
        // In total there will be floor(nTris / 2) such triangles
        float3 botLeftVert = lerp(botLeft, botRight, (float(rowTriIdx - (rowTriIdx / 2))) / (float(row) + 1));
        
        // Prevent divide by zero
        float topT = (float(rowTriIdx - (rowTriIdx / 2))) / float(max(row, 1));
        float3 topVert = lerp(topLeft, topRight, topT);

        float3 botRightVert = lerp(botLeft, botRight, (float(rowTriIdx - (rowTriIdx / 2)) + 1.f) / (float(row) + 1.f));
        
        ret.v0 = GetVertAttribute(EvaluateVertex(botLeftVert, patchIdx));
        ret.v1 = GetVertAttribute(EvaluateVertex(topVert, patchIdx));
        ret.v2 = GetVertAttribute(EvaluateVertex(botRightVert, patchIdx));
    }
    else
    {   
        // Skip inverted triangles when calculating interpolation t which do not contribute in change of horizontal(uw) parameter span
        // In total there will be floor(nTris / 2) such triangles
        float3 topLeftVert = lerp(topLeft, topRight, (float((rowTriIdx - 1) - ((rowTriIdx - 1) / 2))) / float(row));
        float3 topRightVert = lerp(topLeft, topRight, (float(rowTriIdx - (rowTriIdx / 2))) / float(row));
        float3 botVert = lerp(botLeft, botRight, float(rowTriIdx - (rowTriIdx / 2)) / (float(row) + 1.f));
        
        ret.v0 = GetVertAttribute(EvaluateVertex(topLeftVert, patchIdx));
        ret.v1 = GetVertAttribute(EvaluateVertex(topRightVert, patchIdx));
        ret.v2 = GetVertAttribute(EvaluateVertex(botVert, patchIdx));
    }
    
    return ret;
}

// Each MS thread shades a single triangle
[RootSignature(ROOT_SIG)]
[NumThreads(MAX_TRIANGLES_PER_GROUP, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    in payload Payload payload,
    out indices uint3 tris[MAX_TRIANGLES_PER_GROUP],
    out vertices VertexOut verts[MAX_TRIANGLES_PER_GROUP * 3]
)
{
    SetMeshOutputCounts(payload.NumPrimitives[gid] * 3, payload.NumPrimitives[gid]);

    if (gtid < payload.NumPrimitives[gid])
    {
        uint v0Idx = gtid * 3;
        uint v1Idx = v0Idx + 1;
        uint v2Idx = v0Idx + 2;

        tris[gtid] = uint3(v0Idx, v1Idx, v2Idx);

        Triangle tri = GetTriangle(payload.Patch[gid], payload.StartingTriIndices[gid] + gtid);

        verts[v0Idx] = tri.v0;
        verts[v1Idx] = tri.v1;
        verts[v2Idx] = tri.v2;
    }
}
