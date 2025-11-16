#include "Common.hlsl"

struct STriVertex
{
    float3 vertex;
    float4 color;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
  //payload.colorAndDistance = float4(1, 1, 0, RayTCurrent());
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    //const float3 A = float3(1, 0, 0);
    //const float3 B = float3(0, 1, 0);
    //const float3 C = float3(0, 0, 1);

    //float3 hitColor = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;
    
    uint vertId = 3 * PrimitiveIndex();
    //float3 hitColor = BTriVertex[vertId + 0].color * barycentrics.x +
    //              BTriVertex[vertId + 1].color * barycentrics.y +
    //              BTriVertex[vertId + 2].color * barycentrics.z;
    float3 hitColor = BTriVertex[indices[vertId + 0]].color * barycentrics.x +
                    BTriVertex[indices[vertId + 1]].color * barycentrics.y +
                    BTriVertex[indices[vertId + 2]].color * barycentrics.z;
    

    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}
