#include "Common.hlsl"

cbuffer Lights : register(b1)
{
    float3 lightPos;
    float pad1;
    float3 lightColor;
    float pad2;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
StructuredBuffer<ModelInstanceGPU> gInstanceBuffer : register(t2);
RaytracingAccelerationStructure SceneBVH : register(t3);

[shader("closesthit")] 
void ClosestHit_BDSF(inout HitInfo payload, Attributes attrib) 
{
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    uint vertId = 3 * PrimitiveIndex();
    ModelInstanceGPU inst = gInstanceBuffer[BTriVertex[indices[vertId + 0]].id];
    float3 p0 = BTriVertex[indices[vertId + 0]].vertex;
    float3 p1 = BTriVertex[indices[vertId + 1]].vertex;
    float3 p2 = BTriVertex[indices[vertId + 2]].vertex;

    float3 n0 = BTriVertex[indices[vertId + 0]].normal;
    float3 n1 = BTriVertex[indices[vertId + 1]].normal;
    float3 n2 = BTriVertex[indices[vertId + 2]].normal;
    
    float3 hitPosObj = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
    float3 hitPos = mul(ObjectToWorld3x4(), float4(hitPosObj, 1.0f)).xyz;
    float3 hitNormalObj = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
    float3 hitNormal = normalize(mul(hitNormalObj, (float3x3)WorldToObject3x4()));

    float3 lightDir = normalize(lightPos - hitPos);
    float diff = max(dot(hitNormal, lightDir), 0.0f);

    float3 incoming = WorldRayDirection();
    float3 viewDir = normalize(-incoming);
    


    float3 reflectDir = reflect(-viewDir, hitNormal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f); // shininess 32
    if (payload.hopCount == 0 || inst.id != 1)
    {
        float3 baseColor = BTriVertex[indices[vertId + 0]].color.xyz; // or use average of vertices
        float3 ambient = 0.1f * baseColor; // 10% of material color
        float3 finalColor = ambient + baseColor * lightColor * diff + spec * lightColor * 0.2;
        finalColor = saturate(finalColor);
        payload.colorAndDistance = float4(finalColor, RayTCurrent());
    }
    else
    {
        payload.hopCount--;
        float3 incoming = WorldRayDirection();
        float3 reflected = reflect(incoming, hitNormal);
        reflected = normalize(reflected);
        
        float3 newOrigin = hitPos + hitNormal * 0.001f;
        RayDesc ray;
        ray.Origin = newOrigin;
        ray.Direction = reflected;
        ray.TMin = 0;
        ray.TMax = 100000;

        // Trace the ray
        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
		payload.colorAndDistance = 0.5f * payload.colorAndDistance; // darken a bit on each reflection
    }
}