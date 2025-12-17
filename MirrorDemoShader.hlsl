#include "Common.hlsl"

struct STriVertex
{
    float3 vertex;
    float4 color;
    float3 normal;
    int id;
};

cbuffer Lights : register(b1)
{
    float3 lightPos;
    float pad1;
    float3 lightColor;
    float pad2;
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
RaytracingAccelerationStructure SceneBVH : register(t3);

[shader("closesthit")] 
void ClosestHit_MirrorDemo(inout HitInfo payload, Attributes attrib) 
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
        TraceRay(
          // Parameter name: AccelerationStructure
          // Acceleration structure
          SceneBVH,
    
          // Parameter name: RayFlags
          // Flags can be used to specify the behavior upon hitting a surface
          RAY_FLAG_NONE,
    
          // Parameter name: InstanceInclusionMask
          // Instance inclusion mask, which can be used to mask out some geometry to this ray by
          // and-ing the mask with a geometry mask. The 0xFF flag then indicates no geometry will be
          // masked
          0xFF,
    
          // Parameter name: RayContributionToHitGroupIndex
          // Depending on the type of ray, a given object can have several hit groups attached
          // (ie. what to do when hitting to compute regular shading, and what to do when hitting
          // to compute shadows). Those hit groups are specified sequentially in the SBT, so the value
          // below indicates which offset (on 4 bits) to apply to the hit groups for this ray. In this
          // sample we only have one hit group per object, hence an offset of 0.
          0,
    
          // Parameter name: MultiplierForGeometryContributionToHitGroupIndex
          // The offsets in the SBT can be computed from the object ID, its instance ID, but also simply
          // by the order the objects have been pushed in the acceleration structure. This allows the
          // application to group shaders in the SBT in the same order as they are added in the AS, in
          // which case the value below represents the stride (4 bits representing the number of hit
          // groups) between two consecutive objects.
          0,
    
          // Parameter name: MissShaderIndex
          // Index of the miss shader to use in case several consecutive miss shaders are present in the
          // SBT. This allows to change the behavior of the program when no geometry have been hit, for
          // example one to return a sky color for regular rendering, and another returning a full
          // visibility value for shadow rays. This sample has only one miss shader, hence an index 0
          0,
    
          // Parameter name: Ray
          // Ray information to trace
          ray,
    
          // Parameter name: Payload
          // Payload associated to the ray, which will be used to communicate between the hit/miss
          // shaders and the raygen
          payload);
		payload.colorAndDistance -= 0.0f; // darken a bit on each reflection
    }
}


//#include "Common.hlsl"
//
//struct STriVertex
//{
//    float3 vertex;
//    float4 color;
//    float3 normal;
//    int id;
//};
//
//cbuffer Lights : register(b1)
//{
//    float3 lightPos;
//    float pad1;
//    float3 lightColor;
//    float pad2;
//};
//
//struct ModelInstanceGPU
//{
//    float3 testColor;
//    float pad1;
//    int id;
//    float3 pad2;
//};
//
//StructuredBuffer<STriVertex> BTriVertex : register(t0);
//StructuredBuffer<int> indices : register(t1);
//StructuredBuffer<ModelInstanceGPU> gInstanceBuffer : register(t2);
//RaytracingAccelerationStructure SceneBVH : register(t3);
//
//[shader("closesthit")]
//void ClosestHit_MirrorDemo(inout HitInfo payload, Attributes attrib)
//{
//    // barycentrics
//    float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
//
//    uint vertId = 3 * PrimitiveIndex();
//
//    // fetch instance / vertex data
//    STriVertex v0 = BTriVertex[indices[vertId + 0]];
//    STriVertex v1 = BTriVertex[indices[vertId + 1]];
//    STriVertex v2 = BTriVertex[indices[vertId + 2]];
//
//    ModelInstanceGPU inst = gInstanceBuffer[v0.id];
//
//    float3 p0 = v0.vertex;
//    float3 p1 = v1.vertex;
//    float3 p2 = v2.vertex;
//
//    float3 n0 = v0.normal;
//    float3 n1 = v1.normal;
//    float3 n2 = v2.normal;
//
//    // hit position in object space and world space
//    float3 hitPosObj = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
//    float3 hitPos = mul(ObjectToWorld3x4(), float4(hitPosObj, 1.0f)).xyz;
//
//    // interpolate normal in object space
//    float3 hitNormalObj = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
//
//    // Transform normal to world space correctly (use Object->World 3x3)
//    float3x3 objectToWorld3x3 = (float3x3)ObjectToWorld3x4();
//    float3 hitNormal = normalize(mul(objectToWorld3x3, hitNormalObj));
//
//    // lighting terms
//    float3 lightDir = normalize(lightPos - hitPos);
//    float diff = max(dot(hitNormal, lightDir), 0.0f);
//
//    float3 incoming = WorldRayDirection();
//    float3 viewDir = normalize(-incoming);
//
//    // local Phong components
//    float3 baseColor = v0.color.xyz; // you may average v0,v1,v2 if needed
//    float3 ambient = 0.1f * baseColor;
//    float3 reflectDirView = reflect(-viewDir, hitNormal);
//    float specLocal = pow(max(dot(viewDir, reflectDirView), 0.0f), 32.0f);
//
//    float3 localColor = saturate(ambient + baseColor * lightColor * diff + specLocal * lightColor * 0.2f);
//
//    // decide reflection behavior
//    // ensure hopCount > 0 to recurse
//    if (payload.hopCount == 0 || inst.id == 3)
//    {
//        // no recursion: output local Phong result
//        payload.colorAndDistance = float4(localColor, RayTCurrent());
//    }
//    else
//    {
//        // prepare secondary payload (do not overwrite current payload until we combine)
//        HitInfo secondaryPayload;
//        // initialize payload for secondary trace
//        secondaryPayload.colorAndDistance = float4(0.0f, 0.0f, 0.0f, 0.0f);
//        secondaryPayload.hopCount = payload.hopCount - 1; // consume one hop
//
//        // compute reflected direction
//        float3 reflected = normalize(reflect(-incoming, hitNormal));
//
//        // origin offset to avoid self-intersection
//        float3 newOrigin = hitPos + hitNormal * 0.001f;
//
//        RayDesc ray;
//        ray.Origin = newOrigin;
//        ray.Direction = reflected;
//        ray.TMin = 0.001f;        // small epsilon
//        ray.TMax = 1e6f;
//
//        // Trace the reflected ray into the scene using a separate payload
//        TraceRay(
//            SceneBVH,
//            RAY_FLAG_NONE,
//            0xFF,
//            0, 0,
//            0,
//            ray,
//            secondaryPayload);
//
//        // Extract reflected color (if miss shader returns something use it)
//        float3 reflectedColor = secondaryPayload.colorAndDistance.xyz;
//
//        // material reflectivity (1.0 = perfect mirror). adjust to mix local Phong.
//        float reflectivity = 0.95f;
//
//        // final color: mix local lighting and reflected contribution
//        float3 final = lerp(localColor, reflectedColor, reflectivity);
//
//        final = saturate(final);
//
//        payload.colorAndDistance = float4(final, RayTCurrent());
//    }
//}