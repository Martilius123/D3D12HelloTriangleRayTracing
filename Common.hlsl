// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
  float4 colorAndDistance;
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