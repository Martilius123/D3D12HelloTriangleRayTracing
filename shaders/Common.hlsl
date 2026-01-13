#define LIGHT_INTENSITY 1000.0f


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
    uint isInGlass;
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
    int id;
    float emmision;
    float roughness;
    int isMetallic;
    int isGlass;
    float IOR;
    float pad[3];
};

float3 LinearToSRGB(float3 c)
{
    float3 a = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    float3 b = 12.92f * c;
    float3 cond = step(float3(0.0031308f,0.0031308f,0.0031308f), c); // 1 if c>=thr
    return lerp(b, a, cond);
}

float3 RoughnessScatter(float3 reflected, float roughness, uint randomSeed)
{
    // Random samples for hemisphere sampling
    float u1 = RandomFloat(randomSeed);
    float u2 = RandomFloat(randomSeed);

            //Hemisphere sample in local space
    float3 H_local = SampleCosineHemisphere(float2(u1, u2));

            //Build orthonormal basis around the reflected direction
    float3 T, B;
    BuildOrthonormalBasis(reflected, T, B);

    return normalize(lerp(reflected, H_local.x * T + H_local.y * B + H_local.z * reflected, roughness * roughness));
}

void LimitRoughBounces(inout HitInfo payload, float roughness, bool triggeredByGlass=false)
{
    if (roughness >= 0.1)
    {
        if(triggeredByGlass)
        {
            if (payload.isInGlass == 1)
            {
                return; // Do not limit bounces while inside glass
            }
            else
            {
                payload.hopCount = min(payload.hopCount, 3);
            }
        }
        else
        {
            payload.hopCount = min(payload.hopCount, 2); // Limit the remaining hops after rough bounce to 2
        }
    }
}