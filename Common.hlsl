// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
  float4 colorAndDistance;
  uint hopCount;
  uint randomSeed; // used for stochastic effects like rough reflections
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
  float2 bary;
};

float3 TransformLocalToWorld(float3 localVector)
{
    float3x4 objToWorld = ObjectToWorld3x4();
    float3x3 worldRotateScale = (float3x3) objToWorld;
    return normalize(mul(worldRotateScale, localVector));
}

uint InitSeed(uint2 pixel, uint frameIndex)
{
    uint seed = pixel.x * 1973u + pixel.y * 9277u + frameIndex * 26699u;
    return seed | 1u; // avoid zero seed
}

struct STriVertex
{
    float3 vertex;
    float4 color;
    float3 normal;
    float roughness;
    float3 emmision;
    int id;
};

struct ModelInstanceGPU
{
    float3 testColor;
    float pad1;
    int id;
    float3 pad2;
};