#include "Common.hlsl"

Texture2D<float4> envMap : register(t0);
SamplerState envSampler : register(s0);

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    float3 dir = normalize(WorldRayDirection());
    float3 color;
    if (payload.environmentColor.x >= 0)
    {
        color = payload.environmentColor.xyz;
    }
    else
    {
        float u = atan2(dir.z, dir.x) / (2.0 * 3.14159265) + 0.5;
        float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
        float4 hdr = envMap.SampleLevel(envSampler, float2(u, v), 0.0);
        color = hdr.xyz;
    }
    payload.SpecularRadianceAndDistance = payload.colorAndDistance = float4(color, -1.0f);
	payload.DiffuseRadianceAndDistance = float4(0.0f, 0.0f, 0.0f, -1.0f);
	payload.normalAndRoughness = float4(dir, 1.0f);
}
/*
    //payload.colorAndDistance = float4(0.2f, 0.2f, 0.8f, -1.f);
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);

    float ramp = launchIndex.y / dims.y;
    payload.colorAndDistance = float4(0.0f, 0.2f, 0.7f - 0.3f * ramp, -1.0f);
}*/