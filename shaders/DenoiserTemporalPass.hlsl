#include "Common.hlsl"

RWTexture2D<float4> gOutput : register(u0); // beauty/raw
RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDist : register(u2); // spec + hitDist
RWTexture2D<float4> gNormalRoughness : register(u3); // normal + roughness
RWTexture2D<float4> gViewZ : register(u4); // viewZ (for start: -hitDist)
RWTexture2D<float2> gMotion : register(u5);



RWTexture2D<float4> gDiffuseRadianceHitDistHistory : register(u6); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDistHistory : register(u7); // spec + hitDist
RWTexture2D<float4> gNormalRoughnessHistory : register(u8); // normal + roughness
RWTexture2D<float4> gViewZHistory : register(u9);
RWTexture2D<uint> gInstanceID : register(u10);
RWTexture2D<uint> gInstanceIDHistory: register(u11);

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
    // --- Previous frame (history) ---
    float4x4 prevView;
    float4x4 prevProjection;
    float4x4 prevViewProj;
    // --- Current frame derived ---
    float4x4 viewProj;
}

float3 ReconstructWorldPos(uint2 pixel, float viewZ, float2 dims)
{
    // Pixel center - UV
    float2 uv = (float2(pixel) + 0.5f) / dims;
    //float4 vievvvvv = view;
    // UV - NDC
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    // Reconstruct view-space position
    float4 clip = float4(ndc, 1.0f, 1.0f);
    float4 viewPos = mul(projectionI, clip);
    viewPos.xyz /= viewPos.w;

    // Scale by linear view-space depth
    viewPos.xyz *= (-viewZ / viewPos.z);

    // View - world
    float4 worldPos = mul(viewI, float4(viewPos.xyz, 1.0f));
    return worldPos.xyz;
}

float2 ProjectWorldToUV(float3 worldPos, float4x4 viewProj)
{
    float4 clip = mul(viewProj, float4(worldPos, 1.0f));
    float2 ndc = clip.xy / clip.w;

    float2 uv;
    uv.x = ndc.x * 0.5f + 0.5f;
    uv.y = 0.5f - ndc.y * 0.5f;
    return uv;
}


[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    
    float3 specular;
    float3 diffuse;

    // check for invalid history
    bool validHistory = true;
    float depth = gDiffuseRadianceHitDist[pixel].w;
    float prevDepth = gDiffuseRadianceHitDistHistory[pixel].w;
    float depthThreshold = abs(depth) * 0.01f;
    if (abs(depth - prevDepth) > depthThreshold)
    {
        validHistory = false;
    }
    float roughness = gNormalRoughness[pixel].w;
    float prevRoughness = gNormalRoughnessHistory[pixel].w;
    float roughnessThreshold = 0.1f;
    if(abs(roughness - prevRoughness) > roughnessThreshold)
    {
        validHistory = false;
    }
    float3 normal = gNormalRoughness[pixel].xyz;
    float3 prevNormal = gNormalRoughnessHistory[pixel].xyz;
    float normalThreshold = 0.95f;
    if (dot(normal, prevNormal) < normalThreshold)
    {
        validHistory = false;
    }
    uint instanceID = gInstanceID[pixel];
    uint prevInstanceID = gInstanceIDHistory[pixel];
    if (instanceID != prevInstanceID)
    {
        validHistory = false;
    }
    if(instanceID == 1000) // miss shader
    {
        validHistory = false;
    }
    
    // temporal blending
    if (validHistory)
    {
        specular = lerp(gSpecRadianceHitDist[pixel].xyz, gSpecRadianceHitDistHistory[pixel].xyz, 0.85f);
        diffuse = lerp(gDiffuseRadianceHitDist[pixel].xyz, gDiffuseRadianceHitDistHistory[pixel].xyz, 0.85f);
    }  
    else
    {
        specular = gSpecRadianceHitDist[pixel].xyz;
        diffuse = gDiffuseRadianceHitDist[pixel].xyz;
    }

    //setting history
    gDiffuseRadianceHitDistHistory[pixel] = float4(diffuse, gDiffuseRadianceHitDist[pixel].w);
    gSpecRadianceHitDistHistory[pixel] = float4(specular, gSpecRadianceHitDist[pixel].w);
    gNormalRoughnessHistory[pixel] = gNormalRoughness[pixel];
    gViewZHistory[pixel] = gViewZ[pixel];
    gInstanceIDHistory[pixel] = gInstanceID[pixel];
    
    //gOutput[pixel] = float4(LinearToSRGB(diffuse + specular), 0);
    gDiffuseRadianceHitDist[pixel] = float4(diffuse, gDiffuseRadianceHitDist[pixel].w);
    gSpecRadianceHitDist[pixel] = float4(specular, gSpecRadianceHitDist[pixel].w);
    
    /*if(pixel.x % 100 < 50)
        gOutput[pixel] = gDiffuseRadianceHitDist[pixel];
    else
        gOutput[pixel] = gSpecRadianceHitDist[pixel];*/
}
