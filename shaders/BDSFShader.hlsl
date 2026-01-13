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
void ClosestHit_BDSF(inout HitInfo payload : SV_RayPayload, Attributes attrib) 
{
    uint id = InstanceID(); // Now returns 0, 1, 2... based on the C++ loop index
    ModelInstanceGPU inst = gInstanceBuffer[id]; // Correctly fetches the material

    //Shadow Ray Logic
    if(payload.hopCount==-10)//-10marks a shadow ray
    {
        if(inst.isGlass)
        {
            payload.colorAndDistance = float4(0,0,0,-1);
        }
        else
        {
            payload.colorAndDistance = float4(0,0,0,RayTCurrent());
        }
        return;
    }

    //BDSF logic
    float3 barycentrics =
    float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    payload.randomSeed = HashSeed(payload.randomSeed);

    uint vertId = 3 * PrimitiveIndex();
    
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
    payload.normalAndRoughness.xyz = hitNormal;

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
        payload.colorAndDistance.w = RayTCurrent();
        return;
    }
    
    //Roughness
    float roughness = inst.roughness;
    if (inst.roughness < 0)
    {
        //roughness interpolation
        float3 r0 = BTriVertex[indices[vertId + 0]].roughness;
        float3 r1 = BTriVertex[indices[vertId + 1]].roughness;
        float3 r2 = BTriVertex[indices[vertId + 2]].roughness;
        roughness = r0 * barycentrics.x + r1 * barycentrics.y + r2 * barycentrics.z;
    }
    payload.colorAndDistance.w = roughness;
    
    float3 newOrigin;

    // Glass

    if(inst.isGlass&&payload.hopCount>-1)
    {
        payload.hopCount--;  // Decrement the hop count

        newOrigin = hitPos - hitNormal * 0.001f;  // Offset the origin slightly to avoid self-intersection
        //incoming = incoming;
        // Dot product between incoming ray and normal
        float dotI = dot(incoming, hitNormal);
        float sinTheta1 = sqrt(1.0 - dotI * dotI); // sin(theta1)
    
        float n1, n2;
        // Determine whether we are entering or exiting the material
        if (payload.isInGlass==0)
        {
            // Entering the material (ray goes from air to material)
            n1 = 1.0;              // Refractive index of air
            n2 = inst.IOR;         // Refractive index of the material
            payload.isInGlass = 1;
        }
        else
        {
            // Exiting the material (ray goes from material to air)
            n1 = inst.IOR;         // Refractive index of the material
            n2 = 1.0;              // Refractive index of air
            hitNormal = -hitNormal; // Flip the normal for refraction
            payload.isInGlass = 0;
        }
        
        float eta = n1 / n2;
        float cosI = -dot(hitNormal, incoming);
        float sinT2 = eta * eta * (1.0 - cosI * cosI);

        // Check if total internal reflection occurs (sinTheta2 > 1 means no refraction)
        if (false&&sinT2 > 1.0)
        {
            // Total reflection, no refraction
            float3 reflected = reflect(incoming, hitNormal);

            RayDesc ray;
            ray.Origin = hitPos + reflected * 0.001f;
            ray.Direction = reflected;
            ray.TMin = 0.0;
            ray.TMax = 100000.0;
            if (roughness > 0.01f)
            {
                ray.Direction = RoughnessScatter(reflected, roughness, payload.randomSeed);
                LimitRoughBounces(payload, roughness, true);
            }
            
            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        }
        else
        {
            float cosT = sqrt(1.0 - sinT2);
            float3 refracted = eta * incoming + (eta * cosI - cosT) * hitNormal;
            refracted = normalize(refracted);

            RayDesc ray;
            ray.Origin = hitPos + refracted * 0.001f;
            ray.Direction = refracted;
            ray.TMin = 0.0;
            ray.TMax = 100000.0;
            if(roughness>0.01f)
            {
                ray.Direction = RoughnessScatter(refracted, roughness, payload.randomSeed);
                LimitRoughBounces(payload, roughness, true);
            }
            
            payload.colorAndDistance = float4(0, 0, 0, 0);
            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        }
        payload.colorAndDistance.x *= baseColor.x;
        payload.colorAndDistance.y *= baseColor.y;
        payload.colorAndDistance.z *= baseColor.z;
        payload.colorAndDistance.w += RayTCurrent();
        return;
    }
    



    // Solid surface
    //first, we will start with calculating the light contribution
    {
        newOrigin = hitPos + hitNormal * 0.001f;
        float3 shadowRay;
        float3 lightDirection;
        float lightDistance;
        float attenuation;
        if(lightType==0)
        {
            //point light
            shadowRay = lightPos - hitPos;
            lightDirection = normalize(shadowRay);
            lightDistance = length(shadowRay);
            attenuation = LIGHT_INTENSITY / (lightDistance * lightDistance);  // Inverse square attenuation
        }
        else
        if(lightType==1)
        {
            //directional light
            float pitch = lightPos.x * 3.14159265f / 180.0f;
            float yaw = lightPos.y * 3.14159265f / 180.0f;
            float x = cos(pitch) * sin(yaw);
            float y = sin(pitch);
            float z = cos(pitch) * cos(yaw);
            lightDirection = normalize(float3(x,y,z));
            lightDistance = 100000.0f; //infinite
            attenuation = 1.0f; //no attenuation
        }
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


    if (payload.hopCount > -1)
    {
    payload.hopCount--;
        float3 incoming = WorldRayDirection();
        float3 reflected = reflect(incoming, hitNormal);
        reflected = normalize(reflected);
		

        //Starting the preparation of the new ray
        RayDesc ray;
        ray.Origin = newOrigin;
        ray.TMin = 0;
        ray.TMax = 100000;
        
        
        if(inst.isMetallic)
        {
            //METALLIC SURFACE
            
            bool isMirror = false;
            if (roughness < 0.01f)
                isMirror = true;
        // Different behaviour for 0 roughness
            if (isMirror)
            {
			// Perfect mirror reflection
                ray.Direction = reflected;
                payload.colorAndDistance = float4(0, 0, 0, 0);
                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
            }
            else
            {
                LimitRoughBounces(payload, roughness);
			

                float3 l, F;
                do
                {
                    l = ReflectForMetallic(hitNormal, incoming, baseColor, roughness, payload.randomSeed, F);
                } while (l.x == 0 && l.y == 0 && l.z == 0);
            
                ray.Direction = l;

            //ray.Direction = RoughnessScatter(reflected, roughness, payload.randomSeed);
                TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
			
                float NdotV = saturate(dot(hitNormal, viewDir));
                float NdotL = saturate(dot(hitNormal, l));

                float G = G_Smith(NdotV, NdotL, roughness);
                payload.colorAndDistance.xyz *= F * G;
            }
        }
        else
        {
            //DIFFUSE SURFACE
            LimitRoughBounces(payload, roughness);
            float3 l = ReflectDiffuse(hitNormal, payload.randomSeed);
            ray.Direction = l;
            TraceRay(SceneBVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload); // Trace the ray
            
        }
        //payload.colorAndDistance = averageColor / float(sampleCount);
    }
    payload.colorAndDistance.x *= baseColor.x;
    payload.colorAndDistance.y *= baseColor.y;
    payload.colorAndDistance.z *= baseColor.z;
    payload.colorAndDistance.w += RayTCurrent();
}