#include "Common.hlsl"

struct STriVertex
{
    float3 vertex;
    float4 color;
    float3 normal;
    int id;
};
struct ModelInstanceGPU 
{
    float3 testColor;
    float pad1;
    int id;
    float3 pad2;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
StructuredBuffer<ModelInstanceGPU> gInstanceBuffer : register(t2);


[shader("closesthit")] 
void ClosestHit_Flat(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    uint vertId = 3 * PrimitiveIndex();
    float3 hitColor = BTriVertex[indices[vertId + 0]].color * barycentrics.x +
                    BTriVertex[indices[vertId + 1]].color * barycentrics.y +
                    BTriVertex[indices[vertId + 2]].color * barycentrics.z;
    
    ModelInstanceGPU inst = gInstanceBuffer[BTriVertex[indices[vertId + 0]].id];
    
    //inst.testColor = float3(0, 1, 0);
    payload.colorAndDistance = float4(inst.testColor, RayTCurrent());

   // payload.colorAndDistance = float4(gInstanceBuffer.testColor, RayTCurrent());
}
