#include "Common.hlsl"

struct STriVertex
{
    float3 vertex;
    float4 color;
    float3 normal;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);

[shader("closesthit")]
void ClosestHit_Normal(inout HitInfo payload, Attributes attrib)
{
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    uint vertId = 3 * PrimitiveIndex();
    float3 hitColor = BTriVertex[indices[vertId + 0]].normal * barycentrics.x +
                    BTriVertex[indices[vertId + 1]].normal * barycentrics.y +
                    BTriVertex[indices[vertId + 2]].normal * barycentrics.z;
    
    hitColor = TransformLocalToWorld(hitColor);
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}

