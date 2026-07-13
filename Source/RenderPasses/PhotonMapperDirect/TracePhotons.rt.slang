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
    //uint gMaxBounces;     // Max number of bounces allowed, only one in here
    uint gFrameCount;     // Frame count since scene was loaded.
    float gPhotonRadius; // radius for radiance estimate which will be the width for stored AABB

    uint gDispatchDimension;    // Dispatch dims (x,y identical)
    bool kUseEmissive;
    bool kUseAnalytic;
    float gMixedLightsAnalyticProbability;
}

RWStructuredBuffer<AABB> gPhotonAABB;
RWStructuredBuffer<PhotonData> gPhotonData;
//RWStructuredBuffer<uint> gPhotonCounter; we dont do that here

static const uint kPhotonBufferSize = PHOTON_BUFFER_SIZE;

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

    PhotonData photon = PhotonData(); // corresponds to PhotonData Buffer
    photon.flux = pls.flux;
    photon.dir = -ray.Direction;
    photon.normalW = pls.normal;


    TraceRay(gScene.rtAccel, 0 /*Flags*/, 0xff /* instanceInclusionMask */, 0 /* raytype / hitgroupidx */, rayTypeCount, 0 /* missIdx */, ray, rayPayload);


    HitInfo hit = HitInfo(rayPayload.packedHitInfo);
    
    if (!hit.isValid())
        return; //necessary i guess if miss shader doesnt terminate ray, makes sense tough

    // Get material infos from Packed hitinfo
    let lod = ExplicitLodTextureSampler(0);
    ShadingData sd = loadShadingData(hit, ray.Direction, lod);

    uint hints = (uint)MaterialInstanceHints::AdjustShadingNormal;
    
    photon.normalW = sd.getOrientedFaceNormal();
    uint photonInsertIndex = (uint(DispatchRaysIndex().x) * gDispatchDimension) + (uint(DispatchRaysIndex().y)); //unique for every invocation
    if ((photonInsertIndex <= kPhotonBufferSize) && (photonInsertIndex >= 1)) //either the one who wrote <= earlier forgot how indices work, or it actually starts with index 1, and i am not going to take chances
    {
        AABB photonAABB = AABB(sd.posW - gPhotonRadius, sd.posW + gPhotonRadius);
        gPhotonAABB[photonInsertIndex] = photonAABB;
        gPhotonData[photonInsertIndex] = photon;
    }
}
