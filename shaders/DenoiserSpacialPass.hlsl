#include "Common.hlsl"

RWTexture2D<float4> gOutput : register(u0); // beauty/raw
RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDist : register(u2); // spec + hitDist
RWTexture2D<float4> gNormalRoughness : register(u3); // normal + roughness
RWTexture2D<float4> gViewZ : register(u4); // viewZ (for start: -hitDist)
RWTexture2D<float3> gHitPosition : register(u5);



RWTexture2D<float4> gDiffuseRadianceHitDistHistory : register(u6); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDistHistory : register(u7); // spec + hitDist
RWTexture2D<float4> gNormalRoughnessHistory : register(u8); // normal + roughness
RWTexture2D<float4> gViewZHistory : register(u9);
RWTexture2D<uint> gInstanceID : register(u10);
RWTexture2D<uint> gInstanceIDHistory : register(u11);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    
    //setting history
    gDiffuseRadianceHitDistHistory[pixel] = gDiffuseRadianceHitDist[pixel];
    gSpecRadianceHitDistHistory[pixel] = gSpecRadianceHitDist[pixel];
    gNormalRoughnessHistory[pixel] = gNormalRoughness[pixel];
    gViewZHistory[pixel] = gViewZ[pixel];
    gInstanceIDHistory[pixel] = gInstanceID[pixel];

    float3 specular = float3(0, 0, 0);
    float3 diffuse = float3(0, 0, 0);
    
    float centerDepth = gDiffuseRadianceHitDist[pixel].w;
    float centerRoughness = gNormalRoughness[pixel].w;
    float3 centerNormal = gNormalRoughness[pixel].xyz;
    uint centerInstanceID = gInstanceID[pixel];
    
    float depthThreshold = abs(centerDepth) * 0.01f;
    float roughnessThreshold = 0.1f;
    float normalThreshold = 0.99f;
    
    //spacial blending
    uint width, height;
    uint2 neighbor;
    gOutput.GetDimensions(width, height);
    int radius = 3;
    float totalSpecularWeight = 0;
    float totalDiffuseWeight = 0;
    
    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius;dx++)
        {
            neighbor.x = pixel.x + dx;
            neighbor.y = pixel.y + dy;
            if (neighbor.x < 0 || neighbor.x >= width || neighbor.y < 0 || neighbor.y >= height)
                continue;
            //check for edges
            float neighborDepth = gDiffuseRadianceHitDist[neighbor].w;
            float neighborRoughness = gNormalRoughness[neighbor].w;
            float3 neighborNormal = gNormalRoughness[neighbor].xyz;
            uint neighborInstanceID = gInstanceID[neighbor];
            float depthDiff = abs(centerDepth - neighborDepth);
            float depthWeight = exp(-depthDiff * 30);
            float roughnessWeight = lerp(0.2, 1.0, centerRoughness);
            float normalWeight = saturate(dot(centerNormal, neighborNormal));
            float instanceWeight = (centerInstanceID == neighborInstanceID) ? 1.0 : 0.0;
            normalWeight = pow(normalWeight, 32); // sharpen edge rejection
            float dist2 = dx * dx + dy * dy;
            float spatialWeight = exp(-dist2 / 4); // gaussian spatial weight 4 for 7x7 kernel, 2 for 5x5 kernel
            float weight =
                spatialWeight *
                normalWeight *
                depthWeight *
                roughnessWeight *
                instanceWeight;
            if (abs(dx)<=0 && abs(dy)<=0)
            {
                specular += gSpecRadianceHitDist[neighbor].xyz * weight;
                totalSpecularWeight += weight;
            }
            diffuse += gDiffuseRadianceHitDist[neighbor].xyz * weight;
            totalDiffuseWeight += weight;
        }
    }
    specular /= totalSpecularWeight;
    diffuse /= totalDiffuseWeight;

    //setting history
    //gNormalRoughnessHistory[pixel] = gNormalRoughness[pixel];
    //gViewZHistory[pixel] = gViewZ[pixel];
    //gInstanceIDHistory[pixel] = gInstanceID[pixel];
    
    gOutput[pixel] = float4(LinearToSRGB(diffuse + specular), 0);
}
