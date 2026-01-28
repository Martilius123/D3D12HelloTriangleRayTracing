//#include "Common.hlsl"
//#define RAY_FLAG_NONE 0
//
//RWTexture2D<float4> gOutput                 : register(u0); // beauty/raw
//RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse + hitDist
//RWTexture2D<float4> gSpecRadianceHitDist    : register(u2); // spec + hitDist
//RWTexture2D<float4> gNormalRoughness        : register(u3); // normal + roughness
//RWTexture2D<float4> gViewZ                  : register(u4); // viewZ (for start: -hitDist)
//RWTexture2D<float2> gMotion : register(u5);
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
//    
//    gViewZ[launchIndex] = float4(-depthValue, 0, 0, 0);
//}






#include "Common.hlsl"
#define RAY_FLAG_NONE 0

RWTexture2D<float4> gOutput                 : register(u0); // beauty/raw (debug)
RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse radiance + hitDist
RWTexture2D<float4> gSpecRadianceHitDist    : register(u2); // spec radiance + hitDist
RWTexture2D<float4> gNormalRoughness        : register(u3); // normal (world or view) + roughness
RWTexture2D<float4> gViewZ                  : register(u4); // viewZ (negative, linear)
RWTexture2D<float2> gMotion                 : register(u5); // motion vectors (pixels)

RaytracingAccelerationStructure SceneBVH : register(t0);

// IMPORTANT: no bools in CB (match your C++ SceneCB-style packing)
// If you changed C++ CB layout to include ViewProj/PrevViewProj - include here too.
cbuffer CameraParams : register(b0)
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewI;
    float4x4 projectionI;

    float4x4 viewProj;
    float4x4 prevViewProj;

    uint FrameIndex;
    uint SampleCount;
    uint MaxRecursionDepth;
    uint ISOIndex;

    float3 envLightColor;
    uint HighlightOverexposed;      // 0/1 instead of bool
    uint UseEnvLight;               // 0/1 instead of bool
    uint _pad0;
    float2 _pad1;
};

static float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-20f) return float3(0, 0, 1);
    return v * rsqrt(len2);
}

[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 dimsU = DispatchRaysDimensions().xy;
    float2 dims = float2(dimsU);

    // NDC in [-1,1]
    float2 ndc = ((float2(launchIndex) + 0.5f) / dims) * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float3 pixelColor = 0;

    float4 sumDiff = 0;
    float4 sumSpec = 0;
    float4 outNR = float4(0, 0, 1, 1); // normal+roughness fallback
    float  hitT = 0;
    float  viewZ = 0;

    // Motion vectors: compute from camera matrices (no need to trace)
    // We'll compute based on current and previous ViewProj.
    // For best results you want per-pixel world position, but as a START:
    // we use hit position when ray hits; if miss -> 0.
    float2 motion = float2(0, 0);

    HitInfo payload;

    // Trace N samples (if you do path tracing, typically SampleCount=1 for realtime denoise)
    uint sc = max(1u, SampleCount);

    [loop]
        for (uint i = 0; i < sc; ++i)
        {
            // init payload
            payload.colorAndDistance = float4(0, 0, 0, -1);
            payload.hopCount = min(27u, MaxRecursionDepth);
            payload.sampleCount = 1;
            payload.randomSeed = InitSeed(launchIndex, FrameIndex + 1000u * i);
            payload.isInGlass = 0;
            payload.isShadow = 0;

            payload.normalAndRoughness = float4(0, 0, 1, 1);
            payload.DiffuseRadianceAndDistance = float4(0, 0, 0, -1);
            payload.SpecularRadianceAndDistance = float4(0, 0, 0, -1);

            // env color routing
            payload.environmentColor = (UseEnvLight != 0) ? float3(-1.0f, -1.0f, -1.0f) : envLightColor;

            // Ray setup
            RayDesc ray;
            ray.Origin = mul(viewI, float4(0, 0, 0, 1)).xyz;

            // Build direction using inverse projection + inverse view
            float4 target = mul(projectionI, float4(ndc, 1, 1));
            target.xyz /= max(target.w, 1e-6f);

            float3 dirView = SafeNormalize(target.xyz);
            float3 dirWorld = mul((float3x3)viewI, dirView);

            ray.Direction = SafeNormalize(dirWorld);
            ray.TMin = 0.001f;
            ray.TMax = 100000.0f;

            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

            // beauty
            pixelColor += payload.colorAndDistance.rgb;

            // AOVs
            sumDiff += payload.DiffuseRadianceAndDistance;
            sumSpec += payload.SpecularRadianceAndDistance;

            outNR = payload.normalAndRoughness;

            // hit distance from payload.colorAndDistance.w (your convention)
            hitT = payload.colorAndDistance.w;

            // viewZ (linear, negative forward for RH)
            // If we have a hit: compute hit point and transform to view space
            if (hitT > 0.0f)
            {
                float3 hitPos = ray.Origin + ray.Direction * hitT;
                float3 hitPosView = mul(view, float4(hitPos, 1)).xyz;
                viewZ = hitPosView.z; // RH: typically negative in front

                // Motion vectors in pixels:
                float4 curClip = mul(viewProj, float4(hitPos, 1));
                float4 prevClip = mul(prevViewProj, float4(hitPos, 1));

                float2 curNdc = curClip.xy / max(curClip.w, 1e-6f);
                float2 prevNdc = prevClip.xy / max(prevClip.w, 1e-6f);

                // Convert NDC delta to pixel delta (NRD often wants pixels)
                float2 deltaNdc = curNdc - prevNdc;
                motion = deltaNdc * float2(dims.x * 0.5f, dims.y * -0.5f); // y flip
            }
            else
            {
                // miss
                viewZ = -100000.0f;
                motion = float2(0, 0);
            }
        }

    float inv = 1.0f / (float)sc;
    pixelColor *= inv;
    sumDiff *= inv;
    sumSpec *= inv;

    // ISO + SRGB
    pixelColor *= (float)ISOIndex / 400.0f;
    pixelColor = LinearToSRGB(pixelColor);

    float4 beauty = float4(pixelColor, 1.0f);

    if (HighlightOverexposed != 0 && beauty.r > 1.0f)
    {
        if ((launchIndex.x + launchIndex.y) % 20 < 10)
            beauty.rgb = float3(1.0f, 0.95f, 0.0f);
        else
            beauty.rgb = float3(0.0f, 0.0f, 0.0f);
    }

    // Outputs
    gOutput[launchIndex] = beauty;

    // NRD expects hitDist typically in .w for radiance buffers
    // Ensure hitDist is valid even on miss:
    float safeHitDist = (hitT > 0.0f) ? hitT : 0.0f;
    sumDiff.w = safeHitDist;
    sumSpec.w = safeHitDist;

    // normal+roughness: normalize and clamp roughness
    float3 n = SafeNormalize(outNR.xyz);
    float rough = saturate(outNR.w);
    gNormalRoughness[launchIndex] = float4(n, rough);

    // viewZ must be linear and typically negative for forward RH
    // NRD often wants ViewZ in R channel (float)
    gViewZ[launchIndex] = float4(viewZ, 0, 0, 0);

    gDiffuseRadianceHitDist[launchIndex] = sumDiff;
    gSpecRadianceHitDist[launchIndex] = sumSpec;

    gMotion[launchIndex] = motion;
}
