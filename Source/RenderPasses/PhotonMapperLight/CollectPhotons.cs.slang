//compute shader nach vorbild von ResampleReservoirFG schreiben und in der main nutzen nach dem vorbild wie es bei RestirFG in der main benutzt wird

#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.Raytracing;
import Utils.Sampling.SampleGenerator;
import Utils.Color.ColorHelpers;
//import Rendering.RTXDI.RTXDI; shouldnt be needed right now
import StructsAndHelpers;

Texture2D<PackedHitInfo> gVBuffer; //life without the g be like "meh". It really couldnt just be like "vBuffer (yes thats exactly what i am)"

StructuredBuffer<PhotonData> gPhotonData;
StructuredBuffer<AABB> gPhotonAABB;
RaytracingAccelerationStructure gPhotonAS;

RWStructuredBuffer<ReservoirCaustic> gCurrCausticReservoir;
RWTexture2D<float4> gEmission;

static const float kPhotonCollectTMax = 1e-7; // Path length for inverse raduis search (which should be infinitely small)

cbuffer CB
{
    uint gFrameCount; // Frame count since scene was loaded.
    uint2 gFrameDim;  // Frame Dimensions
}

static const float kRoughnessThreshold = ROUGHNESS_THRESHOLD;



bool hitSphere(const float3 center, const float radius, const float3 p) //oh thank you kind sir for riddening me of the burden to think about this
{
    float3 radiusTest = p - center;
    return dot(radiusTest, radiusTest) < radius * radius;
}


//does radiance estimate
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
        const uint primIdx = rayQuery.CandidatePrimitiveIndex();

        // Check if ray origin is inside photon radius
        AABB photonAABB = gPhotonAABB[primIdx]; //TODO: das hier und zeile 58 u.u. falsch
        float radius = (photonAABB.maxPoint.x - photonAABB.minPoint.x) / 2.0;
        if (!hitSphere(photonAABB.center(), radius, sd.posW))
            continue;

        PhotonData photonData = gPhotonData[primIdx];
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


// Creates Caustic Photon reservoir for current frames (that gets used for all Photons though (everything that tries to exclude globals is commented away), not just caustic ones)
void photonResampling(ShadingData sd, BSDFProperties bsdfProperties /*, bool collectGlobalDirect*/, inout ReservoirCaustic causticReservoir, inout SampleGenerator sg) {
    // Prepare photon collection ray
    RayDesc ray;
    ray.Origin = sd.posW;
    ray.Direction = sd.faceN;
    ray.TMin = 0;
    ray.TMax = kPhotonCollectTMax;

    // Use a ray query to collect the photons
    RayQuery<RAY_FLAG_NONE> rayQuery;
    // uint instanceMask = collectGlobalDirect ? 0xFF : 2; // 2 is caustic only
    uint instanceMask = 0xFF;

    rayQuery.TraceRayInline(gPhotonAS, RAY_FLAG_NONE, instanceMask, ray);
    while (rayQuery.Proceed()) {

        if (rayQuery.CandidateType() != CANDIDATE_PROCEDURAL_PRIMITIVE)
            continue;
        //const uint instanceIdx = rayQuery.CandidateInstanceIndex();
        const uint primIdx = rayQuery.CandidatePrimitiveIndex();
        //PhotonType type = (PhotonType)instanceIdx;
        // Check if ray is inside photon radius
        AABB photonAABB = gPhotonAABB/*[instanceIdx]*/[primIdx];
        float radius = (photonAABB.maxPoint.x - photonAABB.minPoint.x) / 2.0;
        float3 center = photonAABB.center();
        if (!hitSphere(center, radius, sd.posW))
            continue;

        PhotonData photonData = gPhotonData/*[instanceIdx]*/[primIdx];

        // If direct global photons should be collected and the photon is global, check the path length
        //every photon should be accepted in here though
        //if (collectGlobalDirect && (type == PhotonType::Global)) {
            //if (photonData.pathLenght > 0)
                //continue;
        //}

        // Check for diffent surfaces (e.g. corners)
        float NdotL = dot(photonData.dir, bsdfProperties.guideNormal);
        if ((dot(photonData.normalW, sd.getOrientedFaceNormal()) < 0.6) || (NdotL <= 0))
            continue;

        // Eval Radiance estimate
        float targetFunction = 0;
        if (NdotL > 0)
            targetFunction = evalApproximateBRDFPhoton(sd, bsdfProperties, photonData.dir) * luminance(photonData.flux);
        float photonDenom = M_PI * radius * radius;
        float w = targetFunction / photonDenom;

        // Update reservoir and resplace sample data if the new sample is selected
        causticReservoir.wSum += w;
        bool selectNew = (sampleNext1D(sg) * causticReservoir.wSum) < w;
        if (selectNew) {
            causticReservoir.photonPosW = center;
            causticReservoir.photonDir = photonData.dir;
            causticReservoir.photonRadiance = photonData.flux;
            causticReservoir.targetFunction = targetFunction;
        }
    }

    // Finalize Reservoir
    if (causticReservoir.targetFunction > 0) {
        causticReservoir.wSum /= causticReservoir.targetFunction;
    }
    else
        causticReservoir.wSum = 0;
    causticReservoir.c = 1;
}


[numthreads(16, 16, 1)] //defines work groups and therefore who can use shared memory, luckily nothing like that happens in here. No screenspace effects here
void main(uint2 pixel: SV_DispatchThreadID)
{
    //uint2 pixel = DispatchRaysIndex().xy;
    uint2 dispatchDims = gFrameDim.xy; //

    HitInfo hitInfo = HitInfo(gVBuffer[pixel]);
    float3 viewDir = -gScene.camera.computeRayPinhole(pixel, dispatchDims).dir;

    ReservoirCaustic causticReservoir = ReservoirCaustic();
    float3 emission = float3(0); // Emission on fully specular paths

    let lod = ExplicitLodTextureSampler(0);
    SampleGenerator sg = SampleGenerator(pixel, gFrameCount * 5 + 1);

    ShadingData sd = loadShadingData(hitInfo, -viewDir, lod);
    uint hints = (uint)MaterialInstanceHints::AdjustShadingNormal;
    let mi = gScene.materials.getMaterialInstance(sd, lod, hints);
    let bsdfProperties = mi.getProperties(sd);

    emission = collectPhotons(sd, mi, bsdfProperties.guideNormal, 0xFF, sg);

    //photons only on surfaces that dont count as specular (otherwise no photon stored, next diffuse hit stored as caustic). Since that logic of not storing on specular surfaces is implemented i keep this check
    bool hitIsRough = bsdfProperties.roughness > kRoughnessThreshold;
    if (hitIsRough) {
        causticReservoir.sampleSurface = hitInfo.getData();
        causticReservoir.sampleSurfaceView = -viewDir;
        photonResampling(sd, bsdfProperties /*, false*/, causticReservoir, sg);
    }
    

    // mit world space reservoirs würde prinzipiell gleichzeitig spacial and temporal gemacht werden,
    // das spatial berücksichtigt aber keine neuen infos dann
    // die neuen infos sollten irgendwie noch ausgetauscht werden
    // vlt screenspace und world space alles einsammeln und kombinieren
    // viel korrelation zwischen world space reservoir (alt) und screen space reservoir an der selben stelle (neu)
    // vlt erst einmal screenspace reservoire einsammeln, und dann beim einsammeln von worldspace reservoiren die rejecten welche
    // die selbe position haben wie bereits genutzte screen space reservoire, so alle neuen infos, trotzdem noch die world space reservoire genutzt
    // andere möglichkeit ist zuerst aktuelles reservoir, das mit temporal im screenspace, zu den worldspace hinzufügen, diese dann einsammeln für spatial

    gCurrCausticReservoir[index2Dto1D(pixel, dispatchDims.x)] = causticReservoir;
    gEmission[pixel] = float4(emission, 1.0);
}

