#include "Common.hlsl"
#define RAY_FLAG_NONE 0

RWTexture2D<float4> gOutput                 : register(u0); // beauty/raw
RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDist    : register(u2); // spec + hitDist
RWTexture2D<float4> gNormalRoughness        : register(u3); // normal + roughness
RWTexture2D<float4> gViewZ                  : register(u4); // viewZ (for start: -hitDist)
RWTexture2D<float2> gMotion                 : register(u5);

RWTexture2D<float4> gDiffuseRadianceHitDistHistory : register(u6); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDistHistory : register(u7); // spec + hitDist
RWTexture2D<float4> gNormalRoughnessHistory : register(u8); // normal + roughness
RWTexture2D<float4> gViewZHistory : register(u9);

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
    HitInfo payload;

    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);

    float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);
    float aspectRatio = dims.x / dims.y;

    float3 pixelColor = float3(0, 0, 0);
    
    float4 outDiffuse = float4(0, 0, 0, 0);
    float4 outSpec = float4(0, 0, 0, 0);
    float4 outNR = float4(0, 0, 1, 0.5); // normal+roughness fallback
    float  outHitDist = 0;

    for (uint i = 0; i < SampleCount; i++)
    {
        payload.colorAndDistance = float4(0, 0, 0, 0);
        payload.hopCount = min(27, MaxRecursionDepth);
        payload.sampleCount = 1;
        payload.randomSeed = InitSeed(launchIndex, FrameIndex + 1000 * i);
        payload.isInGlass = 0;
        payload.isShadow = 0;

        
        if (UseEnvLight)
        {
            payload.environmentColor = float3(-1.0f, -1.0f, -1.0f);
        }
        else
        {
            payload.environmentColor = envLightColor;
        }
        payload.normalAndRoughness = float4(0, 0, 1, 0.5);
        payload.DiffuseRadianceAndDistance = float4(0, 0, 0, -1);
        payload.SpecularRadianceAndDistance = float4(0, 0, 0, -1);
    
        // Define a ray, consisting of origin, direction, and the min-max distance values
    
        
        // Perspective
        RayDesc ray;
        ray.Origin = mul(viewI, float4(0, 0, 0, 10 /*distance*/));
        float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
        ray.Direction = mul(viewI, float4(target.xyz, 0));
        ray.TMin = 0;
        ray.TMax = 100000;

        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

        pixelColor += payload.colorAndDistance.rgb;
        
        outDiffuse += payload.DiffuseRadianceAndDistance;
        outSpec += payload.SpecularRadianceAndDistance;
        outNR = payload.normalAndRoughness;
        outHitDist = payload.colorAndDistance.w;
    }

    pixelColor /= max(1.0, (float)SampleCount);
    outDiffuse /= max(1.0, (float) SampleCount);
    outSpec /= max(1.0, (float) SampleCount);

    // ISO + SRGB
    pixelColor *= ISOIndex / 400.0f;
    pixelColor = LinearToSRGB(pixelColor);

    float4 beauty = float4(pixelColor, 1.0f);

    if (HighlightOverexposed && beauty.r > 1.0f)
    {
        if ((launchIndex.x + launchIndex.y) % 20 < 10)
            beauty.rgb = float3(1.0f, 0.95f, 0.0f);
        else
            beauty.rgb = float3(0.0f, 0.0f, 0.0f);
    }

    gOutput[launchIndex] = beauty;

    float depthValue = min(payload.colorAndDistance.w, 1000.0f);
    gDiffuseRadianceHitDist[launchIndex] = outDiffuse;
    gSpecRadianceHitDist[launchIndex] = outSpec;
    gNormalRoughness[launchIndex] = outNR;
    gMotion[launchIndex] = float2(0.0f, 0.0f);
    gViewZ[launchIndex] = float4(-depthValue, 0, 0, 0);
}
