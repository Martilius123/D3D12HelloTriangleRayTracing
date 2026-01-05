#include "Common.hlsl"

cbuffer Lights : register(b1)
{
    float3 lightPos;
    float lightIntensity;
    float3 lightColor;
    int lightType;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
StructuredBuffer<ModelInstanceGPU> gInstanceBuffer : register(t2);
RaytracingAccelerationStructure SceneBVH : register(t3);

[shader("closesthit")] 
void ClosestHit_BDSF(inout HitInfo payload, Attributes attrib) 
{
    //Shadow Ray Logic
    if(payload.hopCount==-10)//-10marks a shadow ray
    {
        float3 hitColor = float3(1.0f,0,1.0f);
        payload.colorAndDistance = float4(hitColor, RayTCurrent());
        return;
    }

    //BDSF logic
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    payload.randomSeed = HashSeed(payload.randomSeed);

    uint vertId = 3 * PrimitiveIndex();
    //ModelInstanceGPU inst = gInstanceBuffer[BTriVertex[indices[vertId + 0]].id];
    uint id = InstanceID(); // Now returns 0, 1, 2... based on the C++ loop index
    ModelInstanceGPU inst = gInstanceBuffer[id]; // Correctly fetches the material
    
    float3 p0 = BTriVertex[indices[vertId + 0]].vertex;
    float3 p1 = BTriVertex[indices[vertId + 1]].vertex;
    float3 p2 = BTriVertex[indices[vertId + 2]].vertex;

    float3 n0 = BTriVertex[indices[vertId + 0]].normal;
    float3 n1 = BTriVertex[indices[vertId + 1]].normal;
    float3 n2 = BTriVertex[indices[vertId + 2]].normal;
    
    float3 hitPosObj = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
    float3 hitPos = mul(ObjectToWorld3x4(), float4(hitPosObj, 1.0f)).xyz;
    float3 hitNormalObj = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
    float3 hitNormal = normalize(mul(hitNormalObj, (float3x3)WorldToObject3x4()));

    float3 lightDir = normalize(lightPos - hitPos);
    float diff = max(dot(hitNormal, lightDir), 0.0f);

    float3 incoming = WorldRayDirection();
    float3 viewDir = normalize(-incoming);
    


    float3 reflectDir = reflect(-viewDir, hitNormal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), 32.0f); // shininess 32

    // albedo
    float3 baseColor = inst.albedo;
    if(inst.albedo.x<0)
    {
        baseColor = BTriVertex[indices[vertId + 0]].color * barycentrics.x +
            BTriVertex[indices[vertId + 1]].color * barycentrics.y +
            BTriVertex[indices[vertId + 2]].color * barycentrics.z;
    }
    payload.colorAndDistance.xyz = float3(0, 0, 0);

    // Emmision
    if(inst.emmision>0)
    {
        payload.colorAndDistance = float4(baseColor * inst.emmision, RayTCurrent());
        return;
    }
    
    float3 newOrigin = hitPos + hitNormal * 0.001f;

    //first, we will start with calculating the light contribution
    {
        float3 shadowRay = lightPos - hitPos;
        float3 lightDirection = normalize(shadowRay);
        float lightDistance = length(shadowRay);
        float attenuation = 1.0f / (lightDistance * lightDistance);  // Inverse square attenuation
        float diffuseFactor = max(dot(hitNormal, lightDirection), 0.0f);
        HitInfo shadowPayload;
        shadowPayload.colorAndDistance = float4(0, 0, 0, 0);
        shadowPayload.hopCount = -10;
        RayDesc ray;
        ray.Origin = newOrigin;
        ray.TMin = 0;
        ray.TMax = lightDistance;
        ray.Direction = lightDirection;
        TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, shadowPayload);
        if(shadowPayload.colorAndDistance.w<0.0f)
        {
            //light not obstructed
            payload.colorAndDistance+=float4(lightColor * lightIntensity * attenuation * diffuseFactor,0);
        }
    }


    if (payload.hopCount <= 0)
    {
        //End of recursion, Phong shading
        float3 ambient = 0.1f * baseColor; // 10% of material color
        float3 finalColor = ambient + baseColor * lightColor * diff + spec * lightColor * 0.2;
        finalColor = saturate(finalColor);
        finalColor = ambient * 4.0f;
        payload.colorAndDistance += float4(finalColor, RayTCurrent());
    }
    else
    {
        payload.hopCount--;
        float3 incoming = WorldRayDirection();
        float3 reflected = reflect(incoming, hitNormal);
        reflected = normalize(reflected);

        //roughness
        float roughness = inst.roughness;
        if(inst.roughness<0)
        {
            //roughness interpolation
            float3 r0 = BTriVertex[indices[vertId + 0]].roughness;
            float3 r1 = BTriVertex[indices[vertId + 1]].roughness;
            float3 r2 = BTriVertex[indices[vertId + 2]].roughness;
            roughness = r0 * barycentrics.x + r1 * barycentrics.y + r2 * barycentrics.z;
        }
		

        //Starting the preparation of the new ray
        RayDesc ray;
        ray.Origin = newOrigin;
        ray.TMin = 0;
        ray.TMax = 100000;

        // Different behaviour for 0 roughness
        if(roughness < 0.01f)
        {
			// Perfect mirror reflection
            ray.Direction = reflected;
            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
            //payload.colorAndDistance = 0.5f * payload.colorAndDistance; // darken a bit on each reflection
		}
        else
        {
			float4 averageColor = float4(0, 0, 0, 0);
			for (int i = 0; i < 4; i++)
            {
                //change the random seed
                payload.randomSeed = HashSeed(payload.randomSeed);
                // Random samples for hemisphere sampling
                float u1 = RandomFloat(payload.randomSeed);
                float u2 = RandomFloat(payload.randomSeed);

                //Hemisphere sample in local space
                float3 H_local = SampleCosineHemisphere(float2(u1, u2));

                //Build orthonormal basis around the reflected direction
                float3 T, B;
                BuildOrthonormalBasis(reflected, T, B);

                float3 scatteredDir = normalize(lerp(reflected, H_local.x * T + H_local.y * B + H_local.z * reflected, roughness * roughness));
                ray.Direction = scatteredDir;
                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
				averageColor += payload.colorAndDistance;
            }
            payload.colorAndDistance += averageColor / 4.0f;
        }
        payload.colorAndDistance.x *= baseColor.x;
        payload.colorAndDistance.y *= baseColor.y;
        payload.colorAndDistance.z *= baseColor.z;
    }
}

[shader("closesthit")] 
void ClosestHit_Shadow(inout HitInfo payload, Attributes attrib) 
{
    float3 hitColor = float3(1.0f,0,1.0f);
    payload.colorAndDistance = float4(hitColor, RayTCurrent());
}