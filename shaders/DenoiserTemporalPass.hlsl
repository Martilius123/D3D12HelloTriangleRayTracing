#include "Common.hlsl"

RWTexture2D<float4> gOutput : register(u0); // beauty/raw
RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDist : register(u2); // spec + hitDist
RWTexture2D<float4> gNormalRoughness : register(u3); // normal + roughness
RWTexture2D<float4> gViewZ : register(u4); // viewZ (for start: -hitDist)
RWTexture2D<float3> gHitPosition : register(u5);



RWTexture2D<float4> gDiffuseRadianceHitDistHistoryRead : register(u6); // diffuse + hitDist  // for reading data
RWTexture2D<float4> gSpecRadianceHitDistHistoryRead : register(u7); // spec + hitDist
RWTexture2D<float4> gNormalRoughnessHistory : register(u8); // normal + roughness
RWTexture2D<float4> gViewZHistory : register(u9);
RWTexture2D<uint> gInstanceID : register(u10);
RWTexture2D<uint> gInstanceIDHistory : register(u11);

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

float2 ProjectWorldToUV(float3 worldPos, float4x4 viewProj)
{
    // World to Clip
    float4 clip = mul(viewProj, float4(worldPos, 1.0f));

    // Behind the camera or invalid
    if (clip.w <= 0.0f)
        return float2(-1.0f, -1.0f);

    // Clip to NDC
    float2 ndc = clip.xy / clip.w;

    // NDC to UV
    float2 uv;
    uv.x = ndc.x * 0.5f + 0.5f;
    uv.y = 0.5f - ndc.y * 0.5f; // flip Y

    return uv;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    
    uint width, height;
    gViewZ.GetDimensions(width, height);
    float2 dims = float2(width, height);
    
    float3 specular;
    float3 diffuse;
    uint instanceID = gInstanceID[pixel];
    
    // motion
    float2 motion = float2(0, 0);
    
    if (instanceID != MISS_SHADER_INSTANCE_ID)
    {
        float3 worldPos = gHitPosition[pixel].xyz;

        float2 currUV = ProjectWorldToUV(worldPos, viewProj);
        float2 prevUV = ProjectWorldToUV(worldPos, prevViewProj);

        motion = prevUV - currUV;
    }

    // Reproject history
    float2 currUV = (float2(pixel) + 0.5f) / dims;
    float2 prevUV = currUV + motion;
    uint2 prevPixel = uint2(prevUV * dims);
    
    // check for invalid history
    bool validHistory = true;
    // check for pixels out of frame
    if (prevPixel.x < 1 || prevPixel.y < 1 || prevPixel.x >= dims.x || prevPixel.y >= dims.y)
    {
        validHistory = false;
    }
    float depth = gDiffuseRadianceHitDist[pixel].w;
    float prevDepth = gDiffuseRadianceHitDistHistoryRead[prevPixel].w;
    float depthThreshold = abs(depth) * 0.01f;
    if (abs(depth - prevDepth) > depthThreshold)
    {
        validHistory = false;
    }
    float roughness = gNormalRoughness[pixel].w;
    float prevRoughness = gNormalRoughnessHistory[prevPixel].w;
    float roughnessThreshold = 0.1f;
    if (abs(roughness - prevRoughness) > roughnessThreshold)
    {
        validHistory = false;
    }
    float3 normal = gNormalRoughness[pixel].xyz;
    float3 prevNormal = gNormalRoughnessHistory[prevPixel].xyz;
    float normalThreshold = 0.95f;
    if (dot(normal, prevNormal) < normalThreshold)
    {
        validHistory = false;
    }
    uint prevInstanceID = gInstanceIDHistory[prevPixel];
    if (instanceID != prevInstanceID)
    {
        validHistory = false;
    }
    if (instanceID == MISS_SHADER_INSTANCE_ID) // miss shader
    {
        validHistory = false;
    }
    if (FrameIndex == 1)
    {
        validHistory = false;
    }
    
    // temporal blending
    if (validHistory)
    {
        gSpecRadianceHitDist[pixel].xyz = lerp(gSpecRadianceHitDist[pixel].xyz, gSpecRadianceHitDistHistoryRead[prevPixel].xyz, 0.85f);
        gDiffuseRadianceHitDist[pixel].xyz = lerp(gDiffuseRadianceHitDist[pixel].xyz, gDiffuseRadianceHitDistHistoryRead[prevPixel].xyz, 0.85f);
    }
}
