#pragma once
#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"
#include "LightSampler.slang" //the once is because of this

import Scene.Raytracing;
import Utils.Math.MathHelpers;
import Utils.Geometry.GeometryHelpers;
import Utils.Sampling.SampleGenerator;
import Rendering.Lights.LightHelpers;
import StructsAndHelpers;

cbuffer CB
{
    uint gMaxBounces;     // Max number of bounces allowed, only one in here
    uint gFrameCount;     // Frame count since scene was loaded.
    float gPhotonRadius; // radius for radiance estimate which will be the width for stored AABB
    uint gDispatchDimension;    // Dispatch dims (x,y identical)
    bool kUseEmissive;
    bool kUseAnalytic;
    float gMixedLightsAnalyticProbability;
    float gGlobalRejectionProb; // Rejection Probability for global photons
}

RWStructuredBuffer<AABB> gPhotonAABB;
RWStructuredBuffer<PhotonData> gPhotonData;
RWStructuredBuffer<uint> gPhotonCounter;

static const float kRoughnessThreshold = ROUGHNESS_THRESHOLD;
static const uint kPhotonBufferSize = PHOTON_BUFFER_SIZE;
static const bool kRussianRoulette = true;

struct RayPayload //payload is the single output of hit/miss shaders, so it is necessary
{
    PackedHitInfo packedHitInfo; // Hit Info suffices, after single hit raygen extracts flux and allat

    /** Create ray payload with default parameters.
     */
    __init()
    {
        this.packedHitInfo = {};
    }
}

//photon struct aus LightSampler mit normal, dir, flux, pos

//samplerfkt ebenfalls aus light sampler

[shader("miss")]
void miss(inout RayPayload rayData: SV_RayPayload)
{
    HitInfo hitInfo = {};
    rayData.packedHitInfo = hitInfo.getData(); // Invalid hit
}

//no anyhit since i dont care about that

//On closest hit, write necessary info to extract hit into payload so raygen can use it
[shader("closesthit")]
void closestHit(inout RayPayload rayData: SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs: SV_IntersectionAttributes) {
    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;

    rayData.packedHitInfo = HitInfo(triangleHit).getData();
}




[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dispatchDims = DispatchRaysDimensions().xy;

    SampleGenerator sg = SampleGenerator(pixel, gFrameCount * 5);
    RayDesc ray;
    PhotonLightSample pls; //pls end it

    if (!generateLightSample(sg, pls))
        return; // if light sample failed somehow i guess (look when generate light sample returns false)

    //pls.flux /= dispatchDims.x * dispatchDims.y; // because flux is the one of entire light source, since flux gets multiplied by inverse of the chance of choosing it everything checks out

    ray.Direction = pls.dirW; // ray direction given with sample because this function is that chill
    ray.Origin = pls.posW;
    ray.TMin = 0.0;
    ray.TMax = FLT_MAX;
    RayPayload rayPayload = RayPayload();

    bool storeAsCaustic = false;
    bool reflectedDiffuse = false;
    float3 photonStartFlux = pls.flux;
    float3 thp = float3(1.0); // Throughput

    // Prepare photon
    PhotonData photon = PhotonData();
    photon.flux = photonStartFlux;
    photon.dir = -ray.Direction;
    photon.normalW = pls.normal;

    const float invRejection = 1.f / gGlobalRejectionProb;

    // Main Raytrace Loop
    for (uint i = 0; i < gMaxBounces; i++) {
        TraceRay(gScene.rtAccel, 0 /*Flags*/, 0xff /* instanceInclusionMask */, 0 /* hitIdx */, rayTypeCount, 0 /* missIdx */, ray, rayPayload);

        HitInfo hit = HitInfo(rayPayload.packedHitInfo);
        // Stop if photon leaves the scene
        if (!hit.isValid())
            return;

        // Get shading infos and material properties
        let lod = ExplicitLodTextureSampler(0);
        uint hints = (uint)MaterialInstanceHints::AdjustShadingNormal;
        ShadingData sd = loadShadingData(hit, ray.Direction, lod);
        let mi = gScene.materials.getMaterialInstance(sd, lod, hints);
        let bsdfProperties = mi.getProperties(sd);

        // Store Photon if a diffuse surface was hit
        photon.normalW = sd.getOrientedFaceNormal();
        reflectedDiffuse = bsdfProperties.roughness > kRoughnessThreshold;
        if (reflectedDiffuse) {
            PhotonType photonType = storeAsCaustic ? PhotonType::Caustic : PhotonType::Global;
            // Global photons can be rejected. The flux of the stored ones should be increased by the inverse of the rejection probability
            bool storePhoton = true;
            if (photonType == PhotonType::Global) {
                storePhoton = sampleNext1D(sg) <= gGlobalRejectionProb;
                photon.flux *= invRejection;
            }
            // Insert photon into buffer using a counter with interlocked add
            if (storePhoton) {
                uint photonTypeIdx = (uint)photonType;
                uint photonInsertIndex = 0;
                InterlockedAdd(gPhotonCounter[0]/*[photonTypeIdx]*/, 1u, photonInsertIndex);
                if (photonInsertIndex <= kPhotonBufferSize/*[photonTypeIdx]*/)
                {
                    AABB photonAABB = AABB(sd.posW - gPhotonRadius/*[photonTypeIdx]*/, sd.posW + gPhotonRadius[photonTypeIdx]);
                    gPhotonAABB/*[photonTypeIdx]*/[photonInsertIndex] = photonAABB;
                    gPhotonData/*[photonTypeIdx]*/[photonInsertIndex] = photon;
                }
            }
        }

        // Sample Photon bounce for next iteration
        BSDFSample bsdfSample;
        bool validSample = mi.sample(sd, sg, bsdfSample, true /*ImportanceSampling*/);
        if (!validSample)
            return;
        bool transmission = bsdfSample.isLobe(LobeType::Transmission);
        ray.Origin = sd.computeNewRayOrigin(!transmission); // false is for transmissions
        ray.Direction = bsdfSample.wo;
        thp *= bsdfSample.weight;

        // Russian Roulette
        const float rrVal = (thp.x + thp.y + thp.z) / 3.0;
        const float prob = max(0.f, 1.f - rrVal);
        if (sampleNext1D(sg) < prob)
        {
            return; // Photon is absorbed
        }
        else
        {
            thp /= (1.f - prob);
        }

        // Update Photon
        storeAsCaustic = !reflectedDiffuse;
        photon.dir = -ray.Direction;
        photon.flux = photonStartFlux * thp;
        photon.pathLenght++;
    }
}
