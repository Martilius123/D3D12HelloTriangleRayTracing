#include "Common.hlsl"

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
StructuredBuffer<ModelInstanceGPU> gInstanceBuffer : register(t2);


[shader("closesthit")] 
void ClosestHit_Flat(inout HitInfo payload : SV_RayPayload, Attributes attrib) 
{
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    uint vertId = 3 * PrimitiveIndex();
    float3 hitColor = BTriVertex[indices[vertId + 0]].color * barycentrics.x +
                    BTriVertex[indices[vertId + 1]].color * barycentrics.y +
                    BTriVertex[indices[vertId + 2]].color * barycentrics.z;
    
    //ModelInstanceGPU inst = gInstanceBuffer[BTriVertex[indices[vertId + 0]].id];
    
    //payload.colorAndDistance = float4(inst.testColor, RayTCurrent());

    payload.DiffuseRadianceAndDistance = float4(hitColor, RayTCurrent());
}
