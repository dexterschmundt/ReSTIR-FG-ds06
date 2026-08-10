#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.RaytracingInline;
import Utils.Sampling.SampleGenerator;
import Rendering.RTXDI.RTXDI;
import StructsAndHelpers;

Texture2D<PackedHitInfo> gVBuffer;
//StructuredBuffer<ReservoirFG> gFinalGatherReservoir;
StructuredBuffer<ReservoirCaustic> gCausticReservoir;
//Texture2D<float4> gEmission; Value is in color, used as in-/output

RWTexture2D<float4> gColor;

cbuffer CB
{
    uint gFrameCount;         // Current it for sample gen
    uint2 gFrameDim;          // Frame Dimensions for camera dir
}

//static const bool kUseEnvBackground = USE_ENV_BACKROUND;
static const uint kMIHints = (uint)MaterialInstanceHints::AdjustShadingNormal;

/*  Evaluates the final gather reservoir
    \param[in] Current Pixel
    \param[in/out] Random Number Generator
    \return Radiance of the evaluated reserovir
*/
//missing because no FG in this pass

/*  Evaluates the (caustic) photon reservoir
    \param[in] Current Pixel
    \param[in/out] Random Number Generator
    \return Radiance of the evaluated reserovir
*/
float3 evaluateCausticReservoir(uint2 pixel, inout SampleGenerator sg) {
    float3 outColor = float3(0);
    ReservoirCaustic causticReservoir = gCausticReservoir[index2Dto1D(pixel, gFrameDim.x)];
    if (!causticReservoir.isValid())
        return outColor;

    // Get surface and shading data
    HitInfo hitInfo = HitInfo(causticReservoir.sampleSurface);
    let lod = ExplicitLodTextureSampler(0.f);
    ShadingData sd = loadShadingData(hitInfo, causticReservoir.sampleSurfaceView, lod);
    let mi = gScene.materials.getMaterialInstance(sd, lod, kMIHints);
    let bsdfProperties = mi.getProperties(sd);

    float NdotL = dot(bsdfProperties.guideNormal, causticReservoir.photonDir);
    if (NdotL > 0) {
        outColor = causticReservoir.wSum * causticReservoir.photonRadiance * mi.eval(sd, causticReservoir.photonDir, sg);
        outColor /= NdotL; //NdotL needs to be taken out of Falcors BSDF
        outColor *= causticReservoir.thp;
    }

    return outColor;
}

[numthreads(16, 16, 1)]
void main(uint2 pixel: SV_DispatchThreadID)
{
    if(any(pixel >= gFrameDim))
        return;

    HitInfo hitInfo = HitInfo(gVBuffer[pixel]);
    float3 viewDir = gScene.camera.computeRayPinhole(pixel, gFrameDim).dir;

    float3 outColor = float3(0);
    if(hitInfo.isValid()){
        SampleGenerator sg = SampleGenerator(pixel, gFrameCount * 5 + 4);
        let lod = ExplicitLodTextureSampler(0.f);

        // Get Material properties
        ShadingData sd = loadShadingData(hitInfo, viewDir, lod);
        let mi = gScene.materials.getMaterialInstance(sd, lod, kMIHints);
        let bsdfProperties = mi.getProperties(sd);

        //Add Emissive for the directly visible surface
        outColor += bsdfProperties.emission;

        //Eval RTXDI (ReSTIR DI), not used here
        //float dist = 1.f;
        //float3 dir = {};
        //float3 Li = {};
        //bool rtxdiValid = gRTXDI.getFinalSample(pixel, dir, dist, Li);
        
        //if (rtxdiValid)
        {
            //rtxdiValid = dispatchShadowRay(sd, dir, dist);
        }
        //if (rtxdiValid)
        {
            //outColor += mi.eval(sd, dir, sg) * Li;
        }

        //Eval ReSTIR FG or rather just ReSTIR for photons
        //outColor += evaluateFinalGatherReservoir(pixel, sg);
        outColor += evaluateCausticReservoir(pixel, sg);
        // Emissive for (highly) specular materials (path length > 0)
        //outColor += gColor[pixel].xyz;
    }//else{
        //outColor = gScene.envMap.eval(viewDir);
    //}

    gColor[pixel] = float4(outColor, 1.0);
}

// wie is das geplant mit "alles addieren?" Funktioniert das so frfr?
// wie ist das mit dem addieren des radiance estimate gedacht? Sollen das eiglt nur kaustiken sein oder wie? (bei mir sinds jetzt halt alle)
