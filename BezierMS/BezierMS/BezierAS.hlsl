#include "BezierShared.hlsli"

ConstantBuffer<Constants> Globals : register(b0);

[RootSignature(ROOT_SIG)]
[NumThreads(AS_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    // For simplicity each mesh shader thread group only writes 85 primitives(255 verts), so that each thread has to only write one vertex
    // Each mesh shader thread group also only deals with a single patch, regardless of number of primitves to output
    const uint numTrisPerPatch = Globals.NumTesselationRowsPerPatch * Globals.NumTesselationRowsPerPatch;
    const uint numGroupsPerPatch = numTrisPerPatch / MAX_TRIANGLES_PER_GROUP + (((numTrisPerPatch % MAX_TRIANGLES_PER_GROUP) == 0) ? 0 : 1);

    Payload payload = (Payload)0;
    for (uint patchIdx = 0; patchIdx < Globals.NumPatches; ++patchIdx)
    {
        for (uint relGroupIdx = 0; relGroupIdx < numGroupsPerPatch; ++relGroupIdx)
        {
            const uint numTrisProcessed = relGroupIdx * MAX_TRIANGLES_PER_GROUP;
            const uint groupID = patchIdx * numGroupsPerPatch + relGroupIdx;
            
            payload.Patch[groupID] = patchIdx;
            payload.StartingTriIndices[groupID] = numTrisProcessed;
            payload.NumPrimitives[groupID] = min(Globals.NumTrianglesPerPatch - numTrisProcessed, MAX_TRIANGLES_PER_GROUP);
        }
    }
    
    DispatchMesh(numGroupsPerPatch * Globals.NumPatches, 1, 1, payload);
}
