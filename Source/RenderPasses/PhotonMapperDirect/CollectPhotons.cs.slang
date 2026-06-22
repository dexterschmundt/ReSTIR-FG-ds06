//compute shader nach vorbild von ResampleReservoirFG schreiben und in der main nutzen nach dem vorbild wie es bei RestirFG in der main benutzt wird

#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.Raytracing;
import Utils.Sampling.SampleGenerator;
import Utils.Color.ColorHelpers;
import Rendering.RTXDI.RTXDI;
import StructsAndHelpers;

Texture2D<PackedHitInfo> gVBuffer; //life without the g be like "meh". It really couldnt just be like "vBuffer (yes thats exactly what i am)"

StructuredBuffer<PhotonData> gPhotonData;
StructuredBuffer<AABB> gPhotonAABB;
RaytracingAccelerationStructure gPhotonAS;

RWTexture2D<float4> gOutColor;

static const float kPhotonCollectTMax = 1e-7; //guess we will find out what this mf does, otherwise: //TODO: entfernen wenn der nicht genutzt wird

cbuffer CB
{
    uint gFrameCount; // Frame count since scene was loaded.
    uint2 gFrameDim;  // Frame Dimensions
}

bool hitSphere(const float3 center, const float radius, const float3 p) //oh thank you kind sir for riddening me of the burden to think about this
{
    float3 radiusTest = p - center;
    return dot(radiusTest, radiusTest) < radius * radius;
}

float3 collectPhotons(ShadingData sd, IMaterialInstance mi, float3 N, uint instanceMask, inout SampleGenerator sg) {
    float3 collectedRadiance = float3(0);
    // Prepare ray
    RayDesc ray;
    ray.Origin = sd.posW;
    ray.Direction = sd.faceN;
    ray.TMin = 0;
    ray.TMax = kPhotonCollectTMax;

    RayQuery<RAY_FLAG_NONE> rayQuery;

    rayQuery.TraceRayInline(gPhotonAS, RAY_FLAG_NONE, instanceMask, ray);
    while (rayQuery.Proceed()) {

        if (rayQuery.CandidateType() != CANDIDATE_PROCEDURAL_PRIMITIVE)
            continue;
        const uint instanceIdx = rayQuery.CandidateInstanceIndex();
        const uint primIdx = rayQuery.CandidatePrimitiveIndex();

        // Check if ray origin is inside photon radius
        AABB photonAABB = gPhotonAABB[instanceIdx][primIdx];
        float radius = (photonAABB.maxPoint.x - photonAABB.minPoint.x) / 2.0;
        if (!hitSphere(photonAABB.center(), radius, sd.posW))
            continue;

        PhotonData photonData = gPhotonData[instanceIdx][primIdx];
        float NdotL = dot(photonData.dir, N);
        // Check for diffent surfaces (e.g. corners)
        if ((dot(photonData.normalW, sd.faceN) < 0.6) || (NdotL <= 0))
            continue;

        // Radiance for current photon
        float3 bsdf = mi.eval(sd, photonData.dir, sg); //nondeterministic brdf? fr? oh no
        collectedRadiance += max(0, (photonData.flux * bsdf) / (M_PI * radius * radius * NdotL)); // NdotL needs to be taken out of the bsdf for Photon Mapping
    }
    return collectedRadiance;
}

[numthreads(16, 16, 1)] //defines work groups and therefore who can use shared memory, luckily nothing that happens in her. No screenspace effects here
void main(uint2 pixel: SV_DispatchThreadID)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dispatchDims = gFrameDim.xy; //

    HitInfo hitInfo = HitInfo(gVBuffer[pixel]);
    float3 viewDir = -gScene.camera.computeRayPinhole(pixel, dispatchDims).dir;
    float3 outColor = float3(0);

    let lod = ExplicitLodTextureSampler(0);
    SampleGenerator sg = SampleGenerator(pixel, gFrameCount * 5 + 1);

    ShadingData sd = loadShadingData(hitInfo, -viewDir, lod);
    uint hints = (uint)MaterialInstanceHints::AdjustShadingNormal;
    let mi = gScene.materials.getMaterialInstance(sd, lod, hints);
    let bsdfProperties = mi.getProperties(sd);

    outColor = collectPhotons(sd, mi, bsdfProperties.guideNormal, 0xFF, sg);
    gOutColor[pixel] = float4(outColor, 1.0);
}

