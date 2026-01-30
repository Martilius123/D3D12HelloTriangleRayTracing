//#include "Common.hlsl"
//#define RAY_FLAG_NONE 0
//
//RWTexture2D<float4> gOutput                 : register(u0); // beauty/raw
//RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse + hitDist
//RWTexture2D<float4> gSpecRadianceHitDist    : register(u2); // spec + hitDist
//RWTexture2D<float4> gNormalRoughness        : register(u3); // normal + roughness
////RWTexture2D<float4> gViewZ                  : register(u4); // viewZ (for start: -hitDist)
//RWTexture2D<float> gViewZ: register(u4);
//RWTexture2D<float2> gMotion                 : register(u5);
//
//RaytracingAccelerationStructure SceneBVH : register(t0);
//
//cbuffer CameraParams : register(b0)
//{
//    float4x4 view;
//    float4x4 projection;
//    float4x4 viewI;
//    float4x4 projectionI;
//    uint FrameIndex;
//    uint SampleCount;
//    uint MaxRecursionDepth;
//    uint ISOIndex;
//    float3 envLightColor;
//    bool HighlightOverexposed;
//    bool UseEnvLight;
//    float pad[2];
//}
//
//[shader("raygeneration")]
//void RayGen()
//{
//    HitInfo payload;
//
//    uint2 launchIndex = DispatchRaysIndex().xy;
//    float2 dims = float2(DispatchRaysDimensions().xy);
//
//    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);
//    float aspectRatio = dims.x / dims.y;
//
//    float3 pixelColor = float3(0, 0, 0);
//    
//    float4 outDiffuse = float4(0, 0, 0, 0);
//    float4 outSpec = float4(0, 0, 0, 0);
//    float4 outNR = float4(0, 0, 1, 0.5); // normal+roughness fallback
//    float  outHitDist = 0;
//
//    for (uint i = 0; i < SampleCount; i++)
//    {
//        payload.colorAndDistance = float4(0, 0, 0, 0);
//        payload.hopCount = min(27, MaxRecursionDepth);
//        payload.sampleCount = 1;
//        payload.randomSeed = InitSeed(launchIndex, FrameIndex + 1000 * i);
//        payload.isInGlass = 0;
//        payload.isShadow = 0;
//
//        
//        if (UseEnvLight)
//        {
//            payload.environmentColor = float3(-1.0f, -1.0f, -1.0f);
//        }
//        else
//        {
//            payload.environmentColor = envLightColor;
//        }
//        payload.normalAndRoughness = float4(0, 0, 1, 0.5);
//        payload.DiffuseRadianceAndDistance = float4(0, 0, 0, -1);
//        payload.SpecularRadianceAndDistance = float4(0, 0, 0, -1);
//    
//        // Define a ray, consisting of origin, direction, and the min-max distance values
//    
//        
//        // Perspective
//        RayDesc ray;
//        ray.Origin = mul(viewI, float4(0, 0, 0, 10 /*distance*/));
//        float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
//        ray.Direction = mul(viewI, float4(target.xyz, 0));
//        ray.TMin = 0;
//        ray.TMax = 100000;
//
//        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
//
//        pixelColor += payload.colorAndDistance.rgb;
//        
//        outDiffuse += payload.DiffuseRadianceAndDistance;
//        outSpec += payload.SpecularRadianceAndDistance;
//        outNR = payload.normalAndRoughness;
//        outHitDist = payload.colorAndDistance.w;
//    }
//
//    pixelColor /= max(1.0, (float)SampleCount);
//    outDiffuse /= max(1.0, (float) SampleCount);
//    outSpec /= max(1.0, (float) SampleCount);
//
//    // ISO + SRGB
//    pixelColor *= ISOIndex / 400.0f;
//    pixelColor = LinearToSRGB(pixelColor);
//
//    float4 beauty = float4(pixelColor, 1.0f);
//
//    if (HighlightOverexposed && beauty.r > 1.0f)
//    {
//        if ((launchIndex.x + launchIndex.y) % 20 < 10)
//            beauty.rgb = float3(1.0f, 0.95f, 0.0f);
//        else
//            beauty.rgb = float3(0.0f, 0.0f, 0.0f);
//    }
//
//    gOutput[launchIndex] = beauty;
//
//    float depthValue = min(payload.colorAndDistance.w, 1000.0f);
//    gDiffuseRadianceHitDist[launchIndex] = outDiffuse;
//    gSpecRadianceHitDist[launchIndex] = outSpec;
//    gNormalRoughness[launchIndex] = outNR;
//    gMotion[launchIndex] = float2(0.0f, 0.0f);
//    gViewZ[launchIndex] = float4(-depthValue, 0, 0, 0);
//}


#include "Common.hlsl"
#define RAY_FLAG_NONE 0

RWTexture2D<float4> gOutput                 : register(u0); // beauty/raw (do prezentacji)
RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // rgb = radiance, a = hitDist
RWTexture2D<float4> gSpecRadianceHitDist    : register(u2); // rgb = radiance, a = hitDist
RWTexture2D<float4> gNormalRoughness        : register(u3); // xyz = normal (world), w = roughness
RWTexture2D<float>  gViewZ                  : register(u4); // <<< FIX: R32_FLOAT = float
RWTexture2D<float2> gMotion                 : register(u5);

RaytracingAccelerationStructure SceneBVH : register(t0);

cbuffer CameraParams : register(b0)
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewI;
    float4x4 projectionI;
    uint FrameIndex;
    uint SampleCount;
    uint MaxRecursionDepth;
    uint ISOIndex;
    float3 envLightColor;
    bool HighlightOverexposed;
    bool UseEnvLight;
    float pad[2];
}

[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);

    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);

    float3 pixelColor = 0.0;

    float4 sumDiffuse = 0.0;
    float4 sumSpec = 0.0;

    float4 lastNR = float4(0, 0, 1, 0.5);
    float  lastHitDist = 0.0; // keep last valid hit dist

    [loop]
        for (uint i = 0; i < SampleCount; i++)
        {
            HitInfo payload;
            payload.colorAndDistance = float4(0, 0, 0, 0);
            payload.hopCount = min(27, MaxRecursionDepth);
            payload.sampleCount = 1;
            payload.randomSeed = InitSeed(launchIndex, FrameIndex + 1000 * i);
            payload.isInGlass = 0;
            payload.isShadow = 0;

            payload.environmentColor = UseEnvLight ? float3(-1, -1, -1) : envLightColor;

            payload.normalAndRoughness = float4(0, 0, 1, 0.5);

            // IMPORTANT: initialize AOVs with "no hit" as hitDist=0 (NOT -1)
            payload.DiffuseRadianceAndDistance = float4(0, 0, 0, 0);
            payload.SpecularRadianceAndDistance = float4(0, 0, 0, 0);

            RayDesc ray;
            ray.Origin = mul(viewI, float4(0, 0, 0, 1)).xyz;          // camera position
            float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
            ray.Direction = normalize(mul(viewI, float4(target.xyz, 0)).xyz);
            ray.TMin = 0.001;
            ray.TMax = 100000;

            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

            pixelColor += payload.colorAndDistance.rgb;

            // If the shader wrote a valid hit distance, keep it.
            // You set payload.colorAndDistance.w = RayTCurrent() on hit.
            float hitDist = payload.colorAndDistance.w;
            if (hitDist > 0.0)
                lastHitDist = hitDist;

            // Clamp hitDist in AOVs too (avoid negative garbage)
            float dHd = payload.DiffuseRadianceAndDistance.w;
            float sHd = payload.SpecularRadianceAndDistance.w;

            if (dHd < 0.0) payload.DiffuseRadianceAndDistance.w = 0.0;
            if (sHd < 0.0) payload.SpecularRadianceAndDistance.w = 0.0;

            sumDiffuse += payload.DiffuseRadianceAndDistance;
            sumSpec += payload.SpecularRadianceAndDistance;

            lastNR = payload.normalAndRoughness;
        }

    float inv = 1.0 / max(1.0, (float)SampleCount);

    float3 beauty = pixelColor * inv;
    float4 outDiffuse = sumDiffuse * inv;
    float4 outSpec = sumSpec * inv;

    // ISO + SRGB
    beauty *= ISOIndex / 400.0f;
    beauty = LinearToSRGB(beauty);

    if (HighlightOverexposed && beauty.r > 1.0f)
    {
        if ((launchIndex.x + launchIndex.y) % 20 < 10) beauty = float3(1.0f, 0.95f, 0.0f);
        else beauty = 0.0f;
    }

    gOutput[launchIndex] = float4(beauty, 1.0f);

    // AOVs for NRD
    gDiffuseRadianceHitDist[launchIndex] = outDiffuse;
    gSpecRadianceHitDist[launchIndex] = outSpec;
    gNormalRoughness[launchIndex] = lastNR;

    // viewZ: NRD expects linear viewZ (negative in RH view). We'll store -hitDist (>=0 -> <=0).
    float depthValue = min(lastHitDist, 1000.0f);
    gViewZ[launchIndex] = -depthValue;     // <<< FIX: float

    // motion vectors: for now zero = history will be shaky; but at least not UB
    gMotion[launchIndex] = float2(0.0f, 0.0f);
}
