#include "Common.hlsl"

struct STriVertex
{
    float3 vertex;
    float4 color;
    float3 normal;
};

cbuffer Lights : register(b1)
{
    float3 lightPos;
    float pad1;
    float3 lightColor;
    float pad2;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);

[shader("closesthit")] 
void ClosestHit_Phong(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    uint vertId = 3 * PrimitiveIndex();
    float3 p0 = BTriVertex[indices[vertId + 0]].vertex;
    float3 p1 = BTriVertex[indices[vertId + 1]].vertex;
    float3 p2 = BTriVertex[indices[vertId + 2]].vertex;

    float3 n0 = BTriVertex[indices[vertId + 0]].normal;
    float3 n1 = BTriVertex[indices[vertId + 1]].normal;
    float3 n2 = BTriVertex[indices[vertId + 2]].normal;
    
    float3 hitPos = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
    float3 hitNormal = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
    hitNormal = TransformLocalToWorld(hitNormal);
    
    float3 lightDir = normalize(lightPos - hitPos);
    float diff = max(dot(hitNormal, lightDir), 0.0f);
    
    float3 viewDir = normalize(-hitPos); // assuming camera at origin
    float3 reflectDir = reflect(-lightDir, hitNormal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f); // shininess 32

    float3 baseColor = BTriVertex[indices[vertId + 0]].color.xyz; // or use average of vertices
    float3 ambient = 0.1f * baseColor; // 10% of material color
    float3 finalColor = ambient + baseColor * lightColor * diff + spec * lightColor * 0.2;
    finalColor = saturate(finalColor);
    payload.colorAndDistance = float4(finalColor, RayTCurrent());
}
