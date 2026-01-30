Texture2D<float4> gDiffuse : register(t0);
Texture2D<float4> gSpec    : register(t1);
RWTexture2D<float4> gOut   : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    float4 d = gDiffuse[id.xy];
    float4 s = gSpec[id.xy];
    gOut[id.xy] = d + s;
}
