/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "PhotonMapperLightGated.h"

namespace
{
const char kShaderTracePhotons[] = "RenderPasses/PhotonMapperLightGated/TracePhotons.rt.slang";
const char kShaderCollectPhotons[] = "RenderPasses/PhotonMapperLightGated/CollectPhotons.cs.slang";
const char kShaderTemporalResampling[] = "RenderPasses/PhotonMapperLightGated/TemporalResampling.cs.slang";
const char kShaderFinalizeColors[] = "RenderPasses/PhotonMapperLightGated/FinalizeColors.cs.slang";

const std::string kInputVBuffer = "vbuffer"; // so i can just quickly access the texture with render context[thisVariable]
const std::string kInputMotionVectors = "mvec";

const ChannelList kInputChannels = {
    {kInputVBuffer, "gVBuffer", "Visibility buffer in packed format", false, ResourceFormat::RGBA32Uint}, // is the type of VisibilityBuffers supplied by falcor
    {kInputMotionVectors, "gMotionVectors", "Motion vector buffer (float format)",
     false /*but just because i didnt implement it yet. Its not like i want you to be optional or anything, baka!!!*/}, // appearantly i am going insane
};

const std::string kOutputColor = "color";

const ChannelList kOutputChannels = {
    {kOutputColor, "gOutputColor", "Output color ", false, ResourceFormat::RGBA32Float},
};
} // namespace

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, PhotonMapperLightGated>();
}

PhotonMapperLightGated::PhotonMapperLightGated(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    if (!mpDevice->isShaderModelSupported(Device::ShaderModel::SM6_5))
    {
        throw RuntimeError("ReSTIR_FG: Shader Model 6.5 is not supported by the current device");
    }
    if (!mpDevice->isFeatureSupported(Device::SupportedFeatures::RaytracingTier1_1))
    {
        throw RuntimeError("ReSTIR_FG: Raytracing Tier 1.1 is not supported by the current device");
    }

    // making sure NO Spatial resamples
    // mResampleSettings.spatialSamples = 0;
    // wont get passed anywhere anyway


    // Create sample generator only on construction
    mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_UNIFORM);
}

Properties PhotonMapperLightGated::getProperties() const
{
    return {};
}

RenderPassReflection PhotonMapperLightGated::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels); // applies channel lists from above
    addRenderPassOutputs(reflector, kOutputChannels);
    return reflector;
}

void PhotonMapperLightGated::renderUI(Gui::Widgets& widget) {}





void PhotonMapperLightGated::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    // Reset Scene
    mpScene = pScene;

    // Reset all passes and sampling helpers
    mpPhotonAS.reset();
    // mpRTXDI.reset();
    // mResetScreenTex = true;
    // mChangePhotonLightBufferSize = true;

    // mTracePhotonPass = RayTraceProgramHelper::create();
    // mGenerateInitialSamplesPass = RayTraceProgramHelper::create();
    // mpResampleReservoirFGPass.reset();
    // mpResampleReservoirCausticPass.reset();
    // mpEvaluateReservoirsPass.reset();

    if (mpScene)
    {
        if (mpScene->hasGeometryType(Scene::GeometryType::Custom))
        {
            logWarning("This render pass only supports triangles. Other types of geometry will be ignored.");
        }
    }
}






void PhotonMapperLightGated::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mpScene)
        return;
    prepareResources(pRenderContext, renderData);

    // // Clear Photon Counter before tracing the Photons for this frame
    // pRenderContext->clearUAV(mpPhotonCounter->getUAV().get(), uint4(0));
    tracePhotons(pRenderContext, renderData);

    collectPhotons(pRenderContext, renderData);

    TemporalResampling(pRenderContext, renderData);

    FinalizeColors(pRenderContext, renderData);

    mFrameCount++;
    mCanResample = true; // because after one frame Reservoirs are filled

    // renderData holds the requested resources
    // auto& pTexture = renderData.getTexture("src");
}



// stuff that needs to be done every frame before any pass
void PhotonMapperLightGated::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    // all the stuff in if statements could probably be done in h file since this pass doesnt support any changes in params anyway, those
    // will never trigger after being executed once

    auto& pLights = mpScene->getLightCollection(pRenderContext);
    pLights->prepareSyncCPUData(pRenderContext);

    // no analytic lights used stuff, since i just pass it as Variable to shaders that need it, no emmissive sampler fortunately

    // Photon AS and corresponding data
    // if (!mpPhotonAABB) //this and As could probably be done in h file, but i dont care. It gets done only once this way too
    {
        mpPhotonAABB = Buffer::createStructured( // gets filled by shaders with AABB that represent photons with their radius
            mpDevice, sizeof(AABB), mNumMaxPhotons, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
            Buffer::CpuAccess::None, nullptr, false
        );
        mpPhotonAABB->setName("PhotonAABB");
        mpPhotonData = Buffer::createStructured( // contains photon data corresponding to AABB at same index
            mpDevice, sizeof(float) * 12 /*TODO: anpassen auf sowas wie sizeof(photonStruct) wenn ich das mal endlich finde*/,
            mNumMaxPhotons, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, Buffer::CpuAccess::None, nullptr, false
        );
        mpPhotonData->setName("PhotonData");
    }

    // if (!mpPhotonAS) //somehow made everything accumulate indefinitely, so no doing that for AS or ABBBuffer i guess
    {
        std::vector<uint64_t> aabbCount = {mNumMaxPhotons}; // my little guy requires his vectors
        std::vector<uint64_t> aabbGPUAddress = {mpPhotonAABB->getGpuAddress()};
        mpPhotonAS = std::make_unique<CustomAccelerationStructure>(
            mpDevice, aabbCount, aabbGPUAddress, CustomAccelerationStructure::BuildMode::FastBuild,
            CustomAccelerationStructure::UpdateMode::TLASOnly
        );
    }

    // Photon counter
    if (!mpPhotonCounter)
    {
        mpPhotonCounter = Buffer::createStructured(
            mpDevice, sizeof(uint), /*2*/ 1, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
            Buffer::CpuAccess::None, nullptr, false
        );
        mpPhotonCounter->setName("PhotonCounter");

        mpPhotonCounterCPU =
            Buffer::createStructured(mpDevice, sizeof(uint), /*2*/ 1, ResourceBindFlags::None, Buffer::CpuAccess::Read, nullptr, false);
        mpPhotonCounterCPU->setName("PhotonCounterCPU");
    }

    for (uint i = 0; i < 2; i++)
    {
        if (!mpCausticReservoir[i])
        {
            uint2 ScreenDims = renderData.getDefaultTextureDims();
            mCanResample = false;
            mpCausticReservoir[i] = Buffer::createStructured(
                mpDevice, 112u, ScreenDims.x * ScreenDims.y, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
                Buffer::CpuAccess::None, nullptr, false
            );
            mpCausticReservoir[i]->setName("CausticReservoir" + std::to_string(i));
        }
    }
}





// Traces Photons (with indirections this time) and stores them in AABB for inverse radius search
// since i didnt bother with Photon counter there are a lot of old Photons every pass. Didnt produce any problems though so i dont care. Photon counter wont eliminate them anyway since it only estimates, just reduce them, so its ok i guess
void PhotonMapperLightGated::tracePhotons(RenderContext* pRenderContext, const RenderData& renderData)
{

    if (!mTracePhotonPass.pProgram) // create and compile, only if not done yet /could be doen in h file but nah
    {
        // shader library Trace
        RtProgram::Desc desc;
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addShaderLibrary(kShaderTracePhotons);
        desc.setMaxPayloadSize(sizeof(float) * 4 /*change to actual payload size (ka wie groß ich die mache*/);
        desc.setMaxAttributeSize(mpScene->getRaytracingMaxAttributeSize());
        desc.setMaxTraceRecursionDepth(1); // no indirections here, even with indirictions you would just program the raygen shader well
        if (!mpScene->hasProceduralGeometry())
            desc.setPipelineFlags(RtPipelineFlags::SkipProceduralPrimitives);

        mTracePhotonPass = RayTraceProgramHelper::create();

        // binding table (hit groups and how shaders are named) Trace
        mTracePhotonPass.pBindingTable = RtBindingTable::create(
            1 /*count of miss shaders that get chosen by miss index*/, 1 /*count of ray types that chose hit group*/,
            mpScene->getGeometryCount()
        );
        auto& sbt = mTracePhotonPass.pBindingTable;                               // hit groups are just bundles of shaders
        sbt->setRayGen(desc.addRayGen("rayGen", mpScene->getTypeConformances())); // raygen always exactly one, hence outside of hitgroup
        sbt->setMiss(0, desc.addMiss("miss")); // miss determined by assigned miss index, hence outside of hit groups
        if (mpScene->hasGeometryType(Scene::GeometryType::TriangleMesh))
        {
            sbt->setHitGroup(
                0 /*raytype / hitgroupidx */, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), desc.addHitGroup("closestHit")
            );
            // bundles of closest-, any-hit and interesectionshaders, get chosen by combination of raytype / hitgroupidx and geometry type
        }

        // scene defines Trace
        DefineList defines;
        defines.add("USE_EMISSIVE_LIGHT", mpScene->useEmissiveLights() ? "1" : "0"); // always good to know i guess
        defines.add(mpScene->getSceneDefines());                                     // i hope this wont cause any extra considerations

        mTracePhotonPass.pProgram = RtProgram::create(mpDevice, desc, defines);
    }

    // Photon Mapper specific defines Trace
    // mTracePhotonPass.pProgram->addDefine("PHOTON_BUFFER_SIZE", std::to_string(mNumMaxPhotons)); In CB now
    // mTracePhotonPass.pProgram->addDefine("ROUGHNESS_THRESHOLD", std::to_string(mSpecularRoughnessThreshold)); //no caustic vs global   In
    // CB now
    mTracePhotonPass.pProgram->addDefines(getMaterialDefines()); // TODO: add if needed in shader, fkt to add is
    // lower in restir FG lite

    // shadervariables Trace
    mTracePhotonPass.initProgramVars(mpDevice, mpScene, mpSampleGenerator);
    auto var = mTracePhotonPass.pVars->getRootVar();
    mpScene->setRaytracingShaderData(pRenderContext, var);

    var["CB"]["kRoughnessThreshold"] = mSpecularRoughnessThreshold;
    var["CB"]["kPhotonBufferSize"] = mNumMaxPhotons;
    var["CB"]["gFrameCount"] = mFrameCount;
    var["CB"]["gPhotonRadius"] = mPhotonRadius;
    var["CB"]["gMaxBounces"] = mPhotonMaxBounces;               // no bounces
    var["CB"]["gGlobalRejectionProb"] = mGlobalPhotonRejection; // i wont bother with this honestly
    var["CB"]["gDispatchDimension"] = mShaderDispatchDim; // fill buffer up every time, we will see how that works (shoulndt be that bad
                                                          // since raytraced visibility should be much more expensive
    var["CB"]["kUseEmissive"] = mpScene->useEmissiveLights();
    var["CB"]["kUseAnalytic"] = mpScene->useAnalyticLights();
    var["CB"]["gMixedLightsAnalyticProbability"] = mMixedLightsAnalyticProbability;

    // shader output buffers (funily enough as variables)
    var["gPhotonAABB"] = mpPhotonAABB;
    var["gPhotonData"] = mpPhotonData;
    // no photon counter since buffer gets filled up every time
    // current problem with photon counter seems to be that number of photons increases all the time

    // dispatch raytraces (which fill AABB buffer)
    mpScene->raytrace(
        pRenderContext, mTracePhotonPass.pProgram.get(), mTracePhotonPass.pVars,
        uint3(mShaderDispatchDim, mShaderDispatchDim, 1) // Photon buffer size = dispatchdim**2 * num_bounces, each shader creates up to
                                                         // num_bounces photons, so it fills up exactly.
    ); // TODO: if it doesnt pose scheduling problems, just use (mNumMaxPhotons,1,1), or see the almighty reason in the shader (maybe it
       // gets appearant after seeing the source)

    // Clear values after the counter
    // std::vector<ref<Buffer>> aabbs = {mpPhotonAABB /*[0], mpPhotonAABB[1]*/};
    // mpPhotonAS->clearAABBBuffers(pRenderContext, aabbs, true, mpPhotonCounter);

    // Copy counter to CPU
    // handlePhotonCounter(pRenderContext);

    // Build acceleration structure
    // uint/*2*/ currentPhotons = mFrameCount > 0 ? uint/*2*/(float/*2*/(mCurrentPhotonCount) * mASBuildBufferPhotonOverestimate) :
    // mNumMaxPhotons; std::vector<uint64_t> photonBuildSize = { std::min(mNumMaxPhotons /*[0]*/, currentPhotons /*[0]*/) /*,
    // std::min(mNumMaxPhotons[1], currentPhotons[1])*/
    //};
    std::vector<uint64_t> photonBuildSize = {mNumMaxPhotons}; // jedes mal voller build. Wer weis ob das was wird
    mpPhotonAS->update(pRenderContext, photonBuildSize);
}

// handles Photon counter
// isnt used in here currently because it didnt work immediately (while the "big buffer" solution does) and since time = money it will stay like that until someone decides otherwise
void PhotonMapperLightGated::handlePhotonCounter(RenderContext* pRenderContext)
{
    // Copy the photonCounter to a CPU Buffer (asynchronous, read GPU value can be a couple of frames old)
    pRenderContext->copyBufferRegion(mpPhotonCounterCPU.get(), 0, mpPhotonCounter.get(), 0, sizeof(uint /*2*/));

    void* data = mpPhotonCounterCPU->map(Buffer::MapType::Read);
    std::memcpy(&mCurrentPhotonCount, data, sizeof(uint /*2*/));
    mpPhotonCounterCPU->unmap();

    // Code for dynamic dispatch count missing. I wont do that and hope it works //TODO: wenn nicht dann hier ändern, code aus FG_Lite holen
}


// does inverse radius search to get radiance estimate, also computes reservoir for this frame
//  Photons get rejected and stored with logic that differentiates between caustic and global, but everything gets stored as global (with same radius), for that reason the caustic resampling also gets done for all photons (because again time = money and i wont implement a final gather for everything else)
void PhotonMapperLightGated::collectPhotons(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mCollectPhotonPass)
    {
        // shader library collect
        Program::Desc desc;
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addShaderLibrary(kShaderCollectPhotons).csEntry("main").setShaderModel("6_5");
        desc.addTypeConformances(mpScene->getTypeConformances());

        // scene defines collect
        DefineList defines;
        defines.add(mpScene->getSceneDefines());
        defines.add(mpSampleGenerator->getDefines());
        // defines.add("USE_ENV_BACKROUND", mpScene->useEnvBackground() ? "1" : "0");
        defines.add("ROUGHNESS_THRESHOLD", std::to_string(mSpecularRoughnessThreshold)); // wont get changed anyway, so here and not after  the if
        // defines.add(mpRTXDI->getDefines());
        defines.add(getMaterialDefines());

        mCollectPhotonPass = ComputePass::create(mpDevice, desc, defines, true);
    }

    auto var = mCollectPhotonPass->getRootVar();
    mpScene->setRaytracingShaderData(pRenderContext, var);
    mpSampleGenerator->setShaderData(var);

    // Constant Buffer
    var["CB"]["gFrameCount"] = mFrameCount;
    var["CB"]["gFrameDim"] = renderData.getDefaultTextureDims();

    // Input Resources
    var["gVBuffer"] = renderData[kInputVBuffer]->asTexture(); // this seems to be how input textures get accessed
    mpPhotonAS->bindTlas(var, "gPhotonAS");                   // gets assigned in a bit different way i guess
    var["gPhotonAABB"] = mpPhotonAABB;
    var["gPhotonData"] = mpPhotonData;

    // Output ressources
    var["gEmission"] = renderData[kOutputColor]->asTexture();
    var["gCurrCausticReservoir"] = mpCausticReservoir[mFrameCount % 2]; // index for Ping pong rendering

    const uint2 targetDim = renderData.getDefaultTextureDims();
    FALCOR_ASSERT(targetDim.x > 0 && targetDim.y > 0);
    mCollectPhotonPass->execute(pRenderContext, uint3(targetDim, 1));

    // barrier to make sure Reservoirs are filled before further steps
    pRenderContext->uavBarrier(mpCausticReservoir[mFrameCount % 2].get());
}


void PhotonMapperLightGated::TemporalResampling(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mTemporalResamplePass)
    {
        // shader library collect
        Program::Desc desc;
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addShaderLibrary(kShaderTemporalResampling).csEntry("main").setShaderModel("6_5");
        desc.addTypeConformances(mpScene->getTypeConformances());

        // scene defines collect
        DefineList defines;
        defines.add(mpScene->getSceneDefines());
        defines.add(mpSampleGenerator->getDefines());
        // defines.add("USE_ENV_BACKROUND", mpScene->useEnvBackground() ? "1" : "0");
        // defines.add("ROUGHNESS_THRESHOLD", std::to_string(mSpecularRoughnessThreshold)); // wont get changed anyway, so here and not
        // after
        // the if
        // defines.add(mpRTXDI->getDefines());
        defines.add(getMaterialDefines());

        mTemporalResamplePass = ComputePass::create(mpDevice, desc, defines, true);
    }

    // if PrevReservoirCaustic isnt filled, then no resampling possible
    if (!mCanResample)
    {
        return;
    }

    // usual stuff for cs shader
    auto var = mTemporalResamplePass->getRootVar();
    mpScene->setRaytracingShaderData(pRenderContext, var);
    mpSampleGenerator->setShaderData(var);

    // Constant Buffer
    var["CB"]["gFrameCount"] = mFrameCount;
    var["CB"]["gFrameDim"] = renderData.getDefaultTextureDims();
    var["CB"]["gConfidenceLimit"] = mResampleSettings.confidenceCap;
    // var["CB"]["gSpatialRadius"] = mResampleSettings.samplingRadius;
    // var["CB"]["gSpatialSamples"] = mResampleSettings.spatialSamples;
    // var["CB"]["gDisocclusionBoostSpatialSamples"] = mResampleSettings.disocclusionBoostExtraSamples;
    var["CB"]["gNormalThreshold"] = mNormalThreshold;
    var["CB"]["gPhotonRadius"] = mPhotonRadius;

    // Input Resources
    var["gMVec"] = renderData[kInputMotionVectors]->asTexture();
    var["gPrevCausticReservoir"] = mpCausticReservoir[(mFrameCount + 1) % 2];

    // Output ressources
    var["gCurrCausticReservoir"] = mpCausticReservoir[mFrameCount % 2];

    const uint2 targetDim = renderData.getDefaultTextureDims();
    FALCOR_ASSERT(targetDim.x > 0 && targetDim.y > 0);
    mTemporalResamplePass->execute(pRenderContext, uint3(targetDim, 1));
}


void PhotonMapperLightGated::FinalizeColors(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mpFinalizeColorPass)
    {
        Program::Desc desc;
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addShaderLibrary(kShaderFinalizeColors).csEntry("main").setShaderModel("6_5");
        desc.addTypeConformances(mpScene->getTypeConformances());

        DefineList defines;
        defines.add(mpScene->getSceneDefines());
        defines.add(mpSampleGenerator->getDefines());
        // defines.add("USE_ENV_BACKROUND", mpScene->useEnvBackground() ? "1" : "0"); dont want any trouble with that
        // defines.add(mpRTXDI->getDefines());
        defines.add(getMaterialDefines());

        mpFinalizeColorPass = ComputePass::create(mpDevice, desc, defines, true);
    }

    // usual stuff for cs shader
    auto var = mpFinalizeColorPass->getRootVar();
    mpScene->setRaytracingShaderData(pRenderContext, var);
    mpSampleGenerator->setShaderData(var);

    // Constant Buffer
    var["CB"]["gFrameCount"] = mFrameCount;
    var["CB"]["gFrameDim"] = renderData.getDefaultTextureDims();

    // RTXDI resources
    // mpRTXDI->setShaderData(var);

    // Input
    var["gVBuffer"] = renderData[kInputVBuffer]->asTexture();
    // var["gFinalGatherReservoir"] = mpFinalGatherReservoir[mFrameCount % 2];
    var["gCausticReservoir"] = mpCausticReservoir[mFrameCount % 2];
    // var["gEmission"] = renderData[kOutputColor]->asTexture();

    // Output
    var["gColor"] = renderData[kOutputColor]->asTexture(); // emission is also in there

    // Execute
    const uint2 targetDim = renderData.getDefaultTextureDims();
    FALCOR_ASSERT(targetDim.x > 0 && targetDim.y > 0);
    mpFinalizeColorPass->execute(pRenderContext, uint3(targetDim, 1));
}






// funny function that initializes the funny RTXProgram helper that got used in the code i blatantly copied so i use it here too
void PhotonMapperLightGated::RayTraceProgramHelper::initProgramVars(
    ref<Device> pDevice,
    ref<Scene> pScene,
    ref<SampleGenerator> pSampleGenerator
)
{
    FALCOR_ASSERT(pProgram);

    // Configure program.
    pProgram->addDefines(pSampleGenerator->getDefines());
    pProgram->setTypeConformances(pScene->getTypeConformances());
    // Create program variables for the current program.
    // This may trigger shader compilation. If it fails, throw an exception to abort rendering.
    pVars = RtProgramVars::create(pDevice, pProgram, pBindingTable);

    // Bind utility classes into shared data.
    auto var = pVars->getRootVar();
    pSampleGenerator->setShaderData(var);
}

// Ah yes, me when the 5 lines of code may be reused with a probability of the /epsilon used to define continuity so i put it into another function, and the one who copies it is too lazy to deal with the name conflicts that may come up if they just copy the code inline
DefineList PhotonMapperLightGated::getMaterialDefines()
{
    DefineList defines;
    defines.add("DiffuseBrdf", mUseLambertianDiffuse ? "DiffuseBrdfLambert" : "DiffuseBrdfFrostbite");
    defines.add("enableDiffuse", "1");
    defines.add("enableSpecular", "1");
    defines.add("enableTranslucency", "1");
    return defines;
}
