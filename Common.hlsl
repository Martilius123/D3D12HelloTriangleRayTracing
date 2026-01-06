static const float PI = 3.14159265f;

// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
  float4 colorAndDistance;
  int hopCount;
  uint randomSeed; // used for stochastic effects like rough reflections
  uint sampleCount;
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

uint HashSeed(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

float RandomFloat(inout uint state)
{
    state = 1664525u * state + 1013904223u;
    return float(state & 0x00FFFFFF) / float(0x01000000);
}

float3 SampleCosineHemisphere(float2 u)
{
    float r = sqrt(u.x);
    float phi = 2.0 * PI * u.y;

    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(1.0 - u.x);

    return float3(x, y, z);
}

void BuildOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    if (abs(n.z) < 0.999)
        t = normalize(cross(float3(0, 0, 1), n));
    else
        t = normalize(cross(float3(0, 1, 0), n));

    b = cross(n, t);
}


struct STriVertex
{
    float3 vertex;
    float4 color;
    float3 normal;
    float roughness;
    float3 emmision;
    float pad1;
};

struct ModelInstanceGPU
{
    float3 albedo;
    float pad1;
    int id;
    float emmision;
    float roughness;
    float padding;
};

float3 LinearToSRGB(float3 c)
{
    float3 a = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    float3 b = 12.92f * c;
    float3 cond = step(float3(0.0031308f,0.0031308f,0.0031308f), c); // 1 if c>=thr
    return lerp(b, a, cond);
}