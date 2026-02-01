#include "Common.hlsl"

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);

[shader("closesthit")]
void ClosestHit_Normal(inout HitInfo payload : SV_RayPayload, Attributes attrib)
{
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    uint vertId = 3 * PrimitiveIndex();
    float3 hitNormalObj = BTriVertex[indices[vertId + 0]].normal * barycentrics.x +
                    BTriVertex[indices[vertId + 1]].normal * barycentrics.y +
                    BTriVertex[indices[vertId + 2]].normal * barycentrics.z;

    float3 hitColor = normalize(mul(hitNormalObj, (float3x3)WorldToObject3x4()));

    payload.DiffuseRadianceAndDistance = float4(hitColor, RayTCurrent());
}

