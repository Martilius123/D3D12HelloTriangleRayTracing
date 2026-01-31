RWTexture2D<float4> gInput : register(u0); // beauty/raw
RWTexture2D<float4> gDiffuseRadianceHitDist : register(u1); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDist : register(u2); // spec + hitDist
RWTexture2D<float4> gNormalRoughness : register(u3); // normal + roughness
RWTexture2D<float4> gViewZ : register(u4); // viewZ (for start: -hitDist)
RWTexture2D<float2> gMotion : register(u5);



RWTexture2D<float4> gDiffuseRadianceHitDistHistory : register(u6); // diffuse + hitDist
RWTexture2D<float4> gSpecRadianceHitDistHistory : register(u7); // spec + hitDist
RWTexture2D<float4> gNormalRoughnessHistory : register(u8); // normal + roughness
RWTexture2D<float4> gViewZHistory : register(u9);
[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;

    float4 color = gInput[pixel];

    // Darken by 10%
    color.rgb = float3(1.0f, 0.0f, 0.0f);



    gInput[pixel] = color = gDiffuseRadianceHitDistHistory[pixel];

	gDiffuseRadianceHitDistHistory[pixel] = gDiffuseRadianceHitDist[pixel];


    
}
