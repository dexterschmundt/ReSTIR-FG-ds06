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
    float2 gPhotonRadius; // radius for radiance estimate

    uint gDispatchDimension;    // Dispatch dims (x,y identical)
    float gGlobalRejectionProb; // Rejection Probability for global photons
    //bool gUseAnalyticLights;    // true if analytic lights should be used, TODO: add again if samplerfkr needs it
    uint _pad;
}

RWStructuredBuffer<AABB> gPhotonAABB[2];
RWStructuredBuffer<PhotonData> gPhotonData[2];
//RWStructuredBuffer<uint> gPhotonCounter; we dont do that here

static const uint kPhotonBufferSize = PHOTON_BUFFER_SIZE;

//probably dont even need ray payload since i wont be generating subsequent rays

//photon struct aus LightSampler mit normal, dir, flux, pos
