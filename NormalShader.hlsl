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

//#include "Common.hlsl"

//struct STriVertex
//{
//    float3 vertex;
//    float4 color;
//    float3 normal;
//};

//StructuredBuffer<STriVertex> BTriVertex : register(t0);
//StructuredBuffer<int> indices : register(t1);

//[shader("closesthit")]
//void ClosestHit_Normal(inout HitInfo payload, Attributes attrib)
//{
//    float3 barycentrics =
//    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
//    uint vertId = 3 * PrimitiveIndex();

//    // 1. Calculate the Local Normal (Object Space) first
//    float3 localNormal = BTriVertex[indices[vertId + 0]].normal * barycentrics.x +
//                         BTriVertex[indices[vertId + 1]].normal * barycentrics.y +
//                         BTriVertex[indices[vertId + 2]].normal * barycentrics.z;

//    // 2. Get the Instance Transformation Matrix (provided by DXR/Hardware)
//    // This retrieves the matrix from the TLAS instance that was hit
//    float3x4 objToWorld = ObjectToWorld3x4();

//    // 3. Extract the 3x3 rotation/scale portion
//    float3x3 worldMatrix = (float3x3) objToWorld;

//    // 4. Rotate the normal from Local Space to World Space
//    float3 worldNormal = mul(worldMatrix, localNormal);

//    // 5. Normalize it (crucial if your object has scaling applied)
//    worldNormal = normalize(worldNormal);

//    // 6. Use the World Normal as your color
//    payload.colorAndDistance = float4(worldNormal, RayTCurrent());
//}
