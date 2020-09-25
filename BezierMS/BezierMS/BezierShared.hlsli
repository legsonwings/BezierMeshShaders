// Pre-defined threadgroup sizes for AS & MS stages

#define AS_GROUP_SIZE 1
#define MAX_MSGROUPS_PER_ASGROUP 512
#define MAX_TRIANGLES_PER_GROUP 85

#define ROOT_SIG "CBV(b0), \
                  SRV(t0)"

struct Constants
{
    float4x4 World;
    float4x4 WorldView;
    float4x4 WorldViewProj;
    uint NumPatches;
    uint NumTesselationRowsPerPatch;
    uint NumTrianglesPerPatch;
};

struct Payload
{
    uint Patch[MAX_MSGROUPS_PER_ASGROUP];
    uint StartingTriIndices[MAX_MSGROUPS_PER_ASGROUP];
    
    // Can pack these
    uint NumPrimitives[MAX_MSGROUPS_PER_ASGROUP];
};