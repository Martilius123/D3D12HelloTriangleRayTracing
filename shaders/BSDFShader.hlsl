//#include "Common.hlsl"
//
//
//cbuffer Lights : register(b1)
//{
//    float3 lightPos;
//    float lightIntensity;
//    float3 lightColor;
//    int lightType;
//};
//
//StructuredBuffer<STriVertex> BTriVertex : register(t0);
//StructuredBuffer<int> indices : register(t1);
//StructuredBuffer<ModelInstanceGPU> gInstanceBuffer : register(t2);
//RaytracingAccelerationStructure SceneBVH : register(t3);
//
//[shader("closesthit")]
//void ClosestHit_BSDF(inout HitInfo payload : SV_RayPayload, Attributes attrib)
//{
//    uint id = InstanceID(); // Now returns 0, 1, 2... based on the C++ loop index
//    ModelInstanceGPU inst = gInstanceBuffer[id]; // Correctly fetches the material
//
//    float3 incoming = WorldRayDirection();
//    float3 viewDir = normalize(-incoming);
//    
//    uint vertId = 3 * PrimitiveIndex();
//
//    float3 p0 = BTriVertex[indices[vertId + 0]].vertex;
//    float3 p1 = BTriVertex[indices[vertId + 1]].vertex;
//    float3 p2 = BTriVertex[indices[vertId + 2]].vertex;
//    
//    float3 barycentrics =
//        float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
//    
//    float3 hitPosObj = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
//    float3 hitPos = mul(ObjectToWorld3x4(), float4(hitPosObj, 1.0f)).xyz;
//    
//    //Shadow Ray Logic
//    if (payload.isShadow)//-10marks a shadow ray
//    {
//        if (inst.isGlass)
//        {
//            if (payload.hopCount < 1)
//            {
//                payload.colorAndDistance = float4(0, 0, 0, -1);
//            }
//            else
//            {
//                RayDesc ray;
//                ray.Origin = hitPos; // + viewDir * 0.001f;
//                ray.Direction = viewDir;
//                ray.TMin = 0.1;
//                ray.TMax = 100000.0;
//                ray.Direction = viewDir;
//                payload.hopCount--;
//                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
//            }
//        }
//        else
//        {
//            payload.colorAndDistance = float4(0, 0, 0, RayTCurrent());
//        }
//        return;
//    }
//
//    //BDSF logic
//
//    payload.randomSeed = HashSeed(payload.randomSeed);
//
//    float3 n0 = BTriVertex[indices[vertId + 0]].normal;
//    float3 n1 = BTriVertex[indices[vertId + 1]].normal;
//    float3 n2 = BTriVertex[indices[vertId + 2]].normal;
//
//    float3 hitNormalObj = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
//    float3 hitNormal = normalize(mul(hitNormalObj, (float3x3)WorldToObject3x4()));
//    payload.normalAndRoughness.xyz = hitNormal;
//
//    float3 lightDir = normalize(lightPos - hitPos);
//    float diff = max(dot(hitNormal, lightDir), 0.0f);
//    
//
//    float3 reflectDir = reflect(-viewDir, hitNormal);
//    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f); // shininess 32
//
//    // albedo
//    float3 baseColor = inst.albedo;
//    if (inst.albedo.x < 0)
//    {
//        baseColor = BTriVertex[indices[vertId + 0]].color * barycentrics.x +
//            BTriVertex[indices[vertId + 1]].color * barycentrics.y +
//            BTriVertex[indices[vertId + 2]].color * barycentrics.z;
//    }
//    payload.colorAndDistance.xyz = float3(0, 0, 0);
//    float roughness;
//
//    // Emmision
//    if (inst.emmision > 0)
//    {
//        payload.DiffuseRadianceAndDistance = payload.colorAndDistance = float4(baseColor * inst.emmision, RayTCurrent());
//        payload.colorAndDistance.w = RayTCurrent();
//    }
//    else
//    {
//
//        //Roughness
//        roughness = inst.roughness;
//        if (inst.roughness < 0)
//        {
//            //roughness interpolation
//            float3 r0 = BTriVertex[indices[vertId + 0]].roughness;
//            float3 r1 = BTriVertex[indices[vertId + 1]].roughness;
//            float3 r2 = BTriVertex[indices[vertId + 2]].roughness;
//            roughness = r0 * barycentrics.x + r1 * barycentrics.y + r2 * barycentrics.z;
//        }
//        payload.colorAndDistance.w = roughness;
//
//        float3 newOrigin;
//
//        // Glass
//
//        if (inst.isGlass && payload.hopCount > -1)
//        {
//            payload.hopCount--;  // Decrement the hop count
//
//            newOrigin = hitPos - hitNormal * 0.001f;  // Offset the origin slightly to avoid self-intersection
//            //incoming = incoming;
//            // Dot product between incoming ray and normal
//            float dotI = dot(incoming, hitNormal);
//            float sinTheta1 = sqrt(1.0 - dotI * dotI); // sin(theta1)
//
//            float n1, n2;
//            // Determine whether we are entering or exiting the material
//            if (payload.isInGlass == 0)
//            {
//                // Entering the material (ray goes from air to material)
//                n1 = 1.0;              // Refractive index of air
//                n2 = inst.IOR;         // Refractive index of the material
//                payload.isInGlass = 1;
//            }
//            else
//            {
//                // Exiting the material (ray goes from material to air)
//                n1 = inst.IOR;         // Refractive index of the material
//                n2 = 1.0;              // Refractive index of air
//                hitNormal = -hitNormal; // Flip the normal for refraction
//                payload.isInGlass = 0;
//            }
//
//            float eta = n1 / n2;
//            float cosI = -dot(hitNormal, incoming);
//            float sinT2 = eta * eta * (1.0 - cosI * cosI);
//
//            // Check if total internal reflection occurs (sinTheta2 > 1 means no refraction)
//            if (false && sinT2 > 1.0)
//            {
//                // Total reflection, no refraction
//                float3 reflected = reflect(incoming, hitNormal);
//
//                RayDesc ray;
//                ray.Origin = hitPos + reflected * 0.001f;
//                ray.Direction = reflected;
//                ray.TMin = 0.0;
//                ray.TMax = 100000.0;
//                if (roughness > 0.01f)
//                {
//                    ray.Direction = RoughnessScatter(reflected, roughness, payload.randomSeed);
//                    LimitRoughBounces(payload, roughness, true);
//                }
//
//                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
//            }
//            else
//            {
//                float cosT = sqrt(1.0 - sinT2);
//                float3 refracted = eta * incoming + (eta * cosI - cosT) * hitNormal;
//                refracted = normalize(refracted);
//
//                RayDesc ray;
//                ray.Origin = hitPos + refracted * 0.001f;
//                ray.Direction = refracted;
//                ray.TMin = 0.0;
//                ray.TMax = 100000.0;
//                if (roughness > 0.01f)
//                {
//                    ray.Direction = RoughnessScatter(refracted, roughness, payload.randomSeed);
//                    LimitRoughBounces(payload, roughness, true);
//                }
//
//                payload.colorAndDistance = float4(0, 0, 0, 0);
//                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
//            }
//            payload.colorAndDistance.x *= baseColor.x;
//            payload.colorAndDistance.y *= baseColor.y;
//            payload.colorAndDistance.z *= baseColor.z;
//            payload.colorAndDistance.w += RayTCurrent();
//        }
//        else
//        {
//
//
//            // Solid surface
//            newOrigin = hitPos + hitNormal * 0.001f;
//            
//
//            if (payload.hopCount > -1)
//            {
//                payload.hopCount--;
//                float3 incoming = WorldRayDirection();
//                float3 reflected = reflect(incoming, hitNormal);
//                reflected = normalize(reflected);
//
//
//                //Starting the preparation of the new ray
//                RayDesc ray;
//                ray.Origin = newOrigin;
//                ray.TMin = 0;
//                ray.TMax = 100000;
//
//
//                if (inst.isMetallic)
//                {
//                    //METALLIC SURFACE
//
//                    bool isMirror = false;
//                    if (roughness < 0.01f)
//                        isMirror = true;
//                    // Different behaviour for 0 roughness
//                    if (isMirror)
//                    {
//                        // Perfect mirror reflection
//                        ray.Direction = reflected;
//                        payload.colorAndDistance = float4(0, 0, 0, 0);
//                        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
//                    }
//                    else
//                    {
//                        LimitRoughBounces(payload, roughness);
//
//
//                        float3 l, F;
//                        do
//                        {
//                            l = ReflectSpecularMicrofacet(hitNormal, incoming, baseColor, roughness, payload.randomSeed, F);
//                        } while (l.x == 0 && l.y == 0 && l.z == 0);
//
//                        ray.Direction = l;
//
//                        //ray.Direction = RoughnessScatter(reflected, roughness, payload.randomSeed);
//                        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
//
//                        float NdotV = saturate(dot(hitNormal, viewDir));
//                        float NdotL = saturate(dot(hitNormal, l));
//
//                        float G = G_Smith(NdotV, NdotL, roughness);
//                        payload.colorAndDistance.xyz *= F * G;
//                    }
//                    payload.SpecularRadianceAndDistance = payload.colorAndDistance;
//                    payload.DiffuseRadianceAndDistance = float4(0, 0, 0, payload.colorAndDistance.w);
//                }
//                else
//                {
//                    //DIFFUSE SURFACE
//                    LimitRoughBounces(payload, roughness);
//                    //diffuse component
//                    float3 l = ReflectDiffuse(hitNormal, payload.randomSeed);
//                    ray.Direction = l;
//                    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
//                    payload.DiffuseRadianceAndDistance = payload.colorAndDistance;
//                    //specular component
//                    float3 F;
//                    do
//                    {
//                        l = ReflectSpecularMicrofacet(hitNormal, incoming, baseColor, roughness, payload.randomSeed, F);
//                    } while (l.x == 0 && l.y == 0 && l.z == 0);
//                    ray.Direction = l;
//                    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
//                    F = float3(0.04f, 0.04f, 0.04f);
//                    payload.SpecularRadianceAndDistance = payload.colorAndDistance;
//                    float NdotV = saturate(dot(hitNormal, viewDir));
//                    float NdotL = saturate(dot(hitNormal, l));
//                    float G = G_Smith(NdotV, NdotL, roughness);
//                    payload.SpecularRadianceAndDistance.xyz *= F * G;
//                    payload.DiffuseRadianceAndDistance.xyz *= (1 - F);
//                    payload.colorAndDistance = payload.DiffuseRadianceAndDistance + payload.SpecularRadianceAndDistance;
//                }
//                //payload.colorAndDistance = averageColor / float(sampleCount);
//            }
//            
//            
//            //Now we will calculate the point light contribution
//            {
//                float3 shadowRay;
//                float3 lightDirection;
//                float lightDistance;
//                float attenuation;
//                if (lightType == 0)
//                {
//                    //point light
//                    shadowRay = lightPos - hitPos;
//                    lightDirection = normalize(shadowRay);
//                    lightDistance = length(shadowRay);
//                    attenuation = LIGHT_INTENSITY / (lightDistance * lightDistance); // Inverse square attenuation
//                }
//                else if (lightType == 1)
//                {
//                        //directional light
//                    float pitch = lightPos.x * 3.14159265f / 180.0f;
//                    float yaw = lightPos.y * 3.14159265f / 180.0f;
//                    float x = cos(pitch) * sin(yaw);
//                    float y = sin(pitch);
//                    float z = cos(pitch) * cos(yaw);
//                    lightDirection = normalize(float3(x, y, z));
//                    lightDistance = 100000.0f; //infinite
//                    attenuation = 1.0f; //no attenuation
//                }
//                float diffuseFactor = max(dot(hitNormal, lightDirection), 0.0f);
//                HitInfo shadowPayload;
//                shadowPayload.colorAndDistance = float4(0, 0, 0, 0);
//                shadowPayload.isShadow = 1;
//                RayDesc ray;
//                ray.Origin = newOrigin;
//                ray.TMin = 0;
//                ray.TMax = lightDistance;
//                ray.Direction = lightDirection;
//                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, shadowPayload);
//                if (shadowPayload.colorAndDistance.w < 0.0f)
//                {
//                    float3 N = hitNormal;
//                    float3 V = normalize(viewDir);
//                    float3 L = normalize(lightDirection);
//                    float3 H = normalize(V + L);
//
//                    float NdotL = saturate(dot(N, L));
//                    float NdotV = saturate(dot(N, V));
//                    float NdotH = saturate(dot(N, H));
//                    float VdotH = saturate(dot(V, H));
//
//                    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, inst.isMetallic);
//                    float3 F = FresnelSchlick(VdotH, F0);
//
//                    float D = D_GGX(NdotH, roughness);
//                    float G = G_Smith(NdotV, NdotL, roughness);
//
//                    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);
//
//                    float3 kd = (1.0f - F) * (1.0f - inst.isMetallic);
//                    float3 diffuse = kd * baseColor / PI;
//
//                    float3 radiance =
//                        (diffuse + specular) *
//                        lightColor *
//                        lightIntensity *
//                        attenuation *
//                        NdotL;
//
//                    payload.colorAndDistance += float4(radiance, 0);
//                }
//
//            }
//        }
//        payload.colorAndDistance.x *= baseColor.x;
//        payload.colorAndDistance.y *= baseColor.y;
//        payload.colorAndDistance.z *= baseColor.z;
//    }
//    payload.DiffuseRadianceAndDistance.w = payload.SpecularRadianceAndDistance.w = payload.colorAndDistance.w = RayTCurrent();
//    payload.normalAndRoughness = float4(hitNormal, roughness);
//}





#include "Common.hlsl"
#define RAY_FLAG_NONE 0

cbuffer Lights : register(b1)
{
    float3 lightPos;
    float  lightIntensity;
    float3 lightColor;
    int    lightType; // 0 = point, 1 = directional
};

StructuredBuffer<STriVertex>        BTriVertex      : register(t0);
StructuredBuffer<int>              indices         : register(t1);
StructuredBuffer<ModelInstanceGPU> gInstanceBuffer : register(t2);
RaytracingAccelerationStructure    SceneBVH         : register(t3);

static float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-20f) return float3(0, 0, 1);
    return v * rsqrt(len2);
}

// Correct normal transform even for non-uniform scale:
// nWorld = normalize( transpose(WorldToObject3x3) * nObj )
static float3 ObjNormalToWorld(float3 nObj)
{
    float3x3 W2O = (float3x3)WorldToObject3x4();
    return SafeNormalize(mul(transpose(W2O), nObj));
}

[shader("closesthit")]
void ClosestHit_BSDF(inout HitInfo payload : SV_RayPayload, Attributes attrib)
{
    uint id = InstanceID();
    ModelInstanceGPU inst = gInstanceBuffer[id];

    float hitT = RayTCurrent();

    float3 incoming = WorldRayDirection();     // world
    float3 viewDir  = SafeNormalize(-incoming);

    uint vertId = 3u * PrimitiveIndex();

    // ---- Geometry fetch ----
    int i0 = indices[vertId + 0];
    int i1 = indices[vertId + 1];
    int i2 = indices[vertId + 2];

    float3 p0 = BTriVertex[i0].vertex;
    float3 p1 = BTriVertex[i1].vertex;
    float3 p2 = BTriVertex[i2].vertex;

    float3 bary = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

    float3 hitPosObj = p0 * bary.x + p1 * bary.y + p2 * bary.z;
    float3 hitPos    = mul(ObjectToWorld3x4(), float4(hitPosObj, 1.0f)).xyz;

    // ---- Shadow Ray branch (as you had) ----
    if (payload.isShadow)
    {
        // if glass: keep tracing towards camera direction to see if light is blocked
        if (inst.isGlass)
        {
            if (payload.hopCount < 1)
            {
                payload.colorAndDistance = float4(0, 0, 0, -1);
            }
            else
            {
                RayDesc ray;
                ray.Origin    = hitPos;
                ray.Direction = viewDir;
                ray.TMin      = 0.1;
                ray.TMax      = 100000.0;

                payload.hopCount--;
                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
            }
        }
        else
        {
            payload.colorAndDistance = float4(0, 0, 0, hitT);
        }
        return;
    }

    // ---- RNG ----
    payload.randomSeed = HashSeed(payload.randomSeed);

    // ---- Normal (object -> world) ----
    float3 n0 = BTriVertex[i0].normal;
    float3 n1 = BTriVertex[i1].normal;
    float3 n2 = BTriVertex[i2].normal;

    float3 nObj   = SafeNormalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 nWorld = ObjNormalToWorld(nObj);

    // ---- BaseColor ----
    float3 baseColor = inst.albedo;
    if (inst.albedo.x < 0)
    {
        baseColor =
            BTriVertex[i0].color * bary.x +
            BTriVertex[i1].color * bary.y +
            BTriVertex[i2].color * bary.z;
    }

    // ---- Roughness (always initialized) ----
    float roughness = inst.roughness;
    if (roughness < 0)
    {
        // Your vertex roughness is float3; take .x as "the" roughness
        float r0 = BTriVertex[i0].roughness.x;
        float r1 = BTriVertex[i1].roughness.x;
        float r2 = BTriVertex[i2].roughness.x;
        roughness = r0 * bary.x + r1 * bary.y + r2 * bary.z;
    }
    roughness = saturate(roughness);

    // ---- Init outputs (NRD expects these valid) ----
    payload.colorAndDistance             = float4(0, 0, 0, hitT);
    payload.DiffuseRadianceAndDistance   = float4(0, 0, 0, hitT);
    payload.SpecularRadianceAndDistance  = float4(0, 0, 0, hitT);
    payload.normalAndRoughness           = float4(nWorld, roughness);

    // ---- Emission ----
    if (inst.emmision > 0)
    {
        float3 e = baseColor * inst.emmision;
        payload.colorAndDistance.xyz            = e;
        payload.DiffuseRadianceAndDistance.xyz  = e;  // treat emission as diffuse for denoise
        payload.SpecularRadianceAndDistance.xyz = 0;
        return;
    }

    // Small offset to avoid self-intersection
    float3 newOrigin = hitPos + nWorld * 0.001f;

    // ============================================================
    //                  PATH / BSDF (your logic)
    // ============================================================

    // We'll accumulate indirect into these two, then sum.
    float3 indirectDiff = 0;
    float3 indirectSpec = 0;

    // You used hopCount > -1; assumes hopCount is signed int in HitInfo.
    if (payload.hopCount > -1)
    {
        payload.hopCount--;

        // -------- Glass --------
        if (inst.isGlass)
        {
            // offset inside/outside
            float3 originGlass = hitPos - nWorld * 0.001f;

            float n1r, n2r;
            float3 N = nWorld;

            if (payload.isInGlass == 0)
            {
                n1r = 1.0;
                n2r = inst.IOR;
                payload.isInGlass = 1;
            }
            else
            {
                n1r = inst.IOR;
                n2r = 1.0;
                N = -N;
                payload.isInGlass = 0;
            }

            float eta  = n1r / n2r;
            float cosI = -dot(N, incoming);
            float sinT2 = eta * eta * (1.0 - cosI * cosI);

            // NOTE: you had "if(false && sinT2 > 1)" (disabled TIR)
            // We'll keep your behavior: always refract.
            float3 refractedDir;
            if (sinT2 > 1.0f)
            {
                // total internal reflection fallback (just in case)
                refractedDir = reflect(incoming, N);
            }
            else
            {
                float cosT = sqrt(1.0 - sinT2);
                refractedDir = eta * incoming + (eta * cosI - cosT) * N;
            }

            refractedDir = SafeNormalize(refractedDir);

            RayDesc ray;
            ray.Origin    = hitPos + refractedDir * 0.001f;
            ray.Direction = refractedDir;
            ray.TMin      = 0.0;
            ray.TMax      = 100000.0;

            if (roughness > 0.01f)
            {
                ray.Direction = RoughnessScatter(ray.Direction, roughness, payload.randomSeed);
                LimitRoughBounces(payload, roughness, true);
            }

            // Trace
            HitInfo p2;
            p2 = payload;
            p2.colorAndDistance = float4(0,0,0,0);
            p2.DiffuseRadianceAndDistance  = float4(0,0,0,0);
            p2.SpecularRadianceAndDistance = float4(0,0,0,0);

            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p2);

            // Glass treated as specular transport
            indirectSpec += p2.colorAndDistance.xyz * baseColor;
        }
        else
        {
            // -------- Solid --------
            float3 reflected = SafeNormalize(reflect(incoming, nWorld));

            // Metallic / Non-metallic split
            if (inst.isMetallic)
            {
                bool isMirror = (roughness < 0.01f);

                RayDesc ray;
                ray.Origin = newOrigin;
                ray.TMin   = 0.0;
                ray.TMax   = 100000.0;

                if (isMirror)
                {
                    ray.Direction = reflected;

                    HitInfo p2 = payload;
                    p2.colorAndDistance = float4(0,0,0,0);
                    p2.DiffuseRadianceAndDistance  = float4(0,0,0,0);
                    p2.SpecularRadianceAndDistance = float4(0,0,0,0);

                    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p2);
                    indirectSpec += p2.colorAndDistance.xyz; // mirror spec
                }
                else
                {
                    LimitRoughBounces(payload, roughness);

                    float3 l, F;
                    do
                    {
                        l = ReflectSpecularMicrofacet(nWorld, incoming, baseColor, roughness, payload.randomSeed, F);
                    } while (all(l == 0));

                    ray.Direction = SafeNormalize(l);

                    HitInfo p2 = payload;
                    p2.colorAndDistance = float4(0,0,0,0);
                    p2.DiffuseRadianceAndDistance  = float4(0,0,0,0);
                    p2.SpecularRadianceAndDistance = float4(0,0,0,0);

                    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p2);

                    float NdotV = saturate(dot(nWorld, viewDir));
                    float NdotL = saturate(dot(nWorld, ray.Direction));
                    float G = G_Smith(NdotV, NdotL, roughness);

                    indirectSpec += p2.colorAndDistance.xyz * (F * G);
                }
            }
            else
            {
                // Non-metal: do diffuse bounce and spec bounce (your approach)
                LimitRoughBounces(payload, roughness);

                // ---- Diffuse bounce ----
                {
                    float3 l = ReflectDiffuse(nWorld, payload.randomSeed);

                    RayDesc ray;
                    ray.Origin    = newOrigin;
                    ray.Direction = SafeNormalize(l);
                    ray.TMin      = 0.0;
                    ray.TMax      = 100000.0;

                    HitInfo p2 = payload;
                    p2.colorAndDistance = float4(0,0,0,0);
                    p2.DiffuseRadianceAndDistance  = float4(0,0,0,0);
                    p2.SpecularRadianceAndDistance = float4(0,0,0,0);

                    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p2);
                    indirectDiff += p2.colorAndDistance.xyz;
                }

                // ---- Spec bounce ----
                {
                    float3 l, F;
                    do
                    {
                        l = ReflectSpecularMicrofacet(nWorld, incoming, baseColor, roughness, payload.randomSeed, F);
                    } while (all(l == 0));

                    RayDesc ray;
                    ray.Origin    = newOrigin;
                    ray.Direction = SafeNormalize(l);
                    ray.TMin      = 0.0;
                    ray.TMax      = 100000.0;

                    HitInfo p2 = payload;
                    p2.colorAndDistance = float4(0,0,0,0);
                    p2.DiffuseRadianceAndDistance  = float4(0,0,0,0);
                    p2.SpecularRadianceAndDistance = float4(0,0,0,0);

                    TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p2);

                    // You hard-set F = 0.04; keep similar but safer:
                    float3 F0 = float3(0.04f, 0.04f, 0.04f);

                    float NdotV = saturate(dot(nWorld, viewDir));
                    float NdotL = saturate(dot(nWorld, ray.Direction));
                    float G = G_Smith(NdotV, NdotL, roughness);

                    indirectSpec += p2.colorAndDistance.xyz * (F0 * G);
                }
            }
        }
    }

    // Apply baseColor split like in PBR:
    // - diffuse multiplied by baseColor
    // - spec uses baseColor only if metallic; otherwise F0 is ~0.04
    float metallic = inst.isMetallic ? 1.0f : 0.0f;

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);

    // For indirect we keep your older “multiply by baseColor at end” vibe,
    // but do it more correctly:
    float3 indirectDiffuseOut = indirectDiff * baseColor;
    float3 indirectSpecOut    = indirectSpec; // already weighted above; keep

    // ============================================================
    //                 DIRECT LIGHT (your block, fixed)
    // ============================================================

    float3 directDiff = 0;
    float3 directSpec = 0;

    {
        float3 L;
        float  lightDistance;
        float  attenuation;

        if (lightType == 0)
        {
            float3 toL = lightPos - hitPos;
            lightDistance = length(toL);
            L = (lightDistance > 1e-6f) ? (toL / lightDistance) : float3(0,1,0);

            // FIX: use lightIntensity, not LIGHT_INTENSITY macro
            attenuation = lightIntensity / max(lightDistance * lightDistance, 1e-6f);
        }
        else
        {
            // directional: interpret lightPos.xy as (pitch,yaw) degrees like you had
            float pitch = lightPos.x * 3.14159265f / 180.0f;
            float yaw   = lightPos.y * 3.14159265f / 180.0f;

            float x = cos(pitch) * sin(yaw);
            float y = sin(pitch);
            float z = cos(pitch) * cos(yaw);

            L = SafeNormalize(float3(x, y, z));
            lightDistance = 100000.0f;
            attenuation = lightIntensity; // directional intensity scaling
        }

        float NdotL = saturate(dot(nWorld, L));
        if (NdotL > 0.0f)
        {
            // Shadow ray
            HitInfo shadowPayload;
            shadowPayload.colorAndDistance = float4(0, 0, 0, 0);
            shadowPayload.isShadow = 1;
            shadowPayload.hopCount = 0; // doesn't matter much for shadow
            shadowPayload.isInGlass = payload.isInGlass;

            RayDesc sray;
            sray.Origin    = newOrigin;
            sray.Direction = L;
            sray.TMin      = 0.0;
            sray.TMax      = lightDistance;

            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, sray, shadowPayload);

            // Your convention: miss => w < 0
            if (shadowPayload.colorAndDistance.w < 0.0f)
            {
                float3 N = nWorld;
                float3 V = viewDir;
                float3 H = SafeNormalize(V + L);

                float NdotV = saturate(dot(N, V));
                float NdotH = saturate(dot(N, H));
                float VdotH = saturate(dot(V, H));

                float3 F = FresnelSchlick(VdotH, F0);
                float  D = D_GGX(NdotH, roughness);
                float  G = G_Smith(NdotV, NdotL, roughness);

                float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

                float3 kd = (1.0f - F) * (1.0f - metallic);
                float3 diffuse = kd * baseColor / PI;

                float3 radiance = lightColor * attenuation * NdotL;

                directDiff += diffuse * radiance;
                directSpec += specular * radiance;
            }
        }
    }

    // ============================================================
    //                     WRITE OUTPUTS
    // ============================================================

    float3 outDiff = indirectDiffuseOut + directDiff;
    float3 outSpec = indirectSpecOut    + directSpec;

    payload.DiffuseRadianceAndDistance.xyz  = outDiff;
    payload.SpecularRadianceAndDistance.xyz = outSpec;

    payload.colorAndDistance.xyz = outDiff + outSpec;

    payload.DiffuseRadianceAndDistance.w  =
    payload.SpecularRadianceAndDistance.w =
    payload.colorAndDistance.w            = hitT;

    payload.normalAndRoughness = float4(nWorld, roughness);
}
