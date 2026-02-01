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
[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

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
