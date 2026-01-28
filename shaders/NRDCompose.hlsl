// shaders/NRDCompose.hlsl
// Compose denoised diffuse+spec into UNORM output with simple exposure + SRGB

Texture2D<float4> gDiff : register(t0); // denoised diffuse (linear HDR)
Texture2D<float4> gSpec : register(t1); // denoised spec   (linear HDR)
RWTexture2D<float4> gOut : register(u0); // R8G8B8A8_UNORM UAV

cbuffer ComposeCB : register(b0)
{
    uint  ISOIndex;
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
}

static float3 LinearToSRGB(float3 x)
{
    // same as typical SRGB curve
    float3 lo = x * 12.92;
    float3 hi = 1.055 * pow(max(x, 0.0), 1.0 / 2.4) - 0.055;
    return (x <= 0.0031308) ? lo : hi;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint2 p = dtid.xy;

    float3 diff = gDiff[p].rgb;
    float3 spec = gSpec[p].rgb;

    float3 color = diff + spec;

    // exposure similar to your raygen
    color *= (float)ISOIndex / 400.0f;

    // clamp to something sane before SRGB
    color = max(color, 0.0);
    color = LinearToSRGB(color);

    gOut[p] = float4(color, 1.0);
}
