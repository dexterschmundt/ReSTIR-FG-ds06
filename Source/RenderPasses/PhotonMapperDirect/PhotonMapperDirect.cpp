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
#include "PhotonMapperDirect.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Rendering/AccelerationStructure/CustomAccelerationStructure.h"

namespace
{
const char kShaderTracePhotons[] = "RenderPasses/PhotonMapperDirect/TracePhotons.rt.slang";

const ChannelList kInputChannels = {
    {"vbuffer", "gVBuffer", "Visibility buffer in packed format", false, ResourceFormat::RGBA32Uint}, // is the type of VisibilityBuffers supplied by
                                                                                                      // falcor
};

const ChannelList kOutputChannels = {
    {"color", "gOutputColor", "Output color ", false, ResourceFormat::RGBA32Float},
};
} // namespace








extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, PhotonMapperDirect>();
}

PhotonMapperDirect::PhotonMapperDirect(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
}

Properties PhotonMapperDirect::getProperties() const
{
    return {};
}

void PhotonMapperDirect::renderUI(Gui::Widgets& widget) {}

RenderPassReflection PhotonMapperDirect::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels); // applies channel lists from above
    addRenderPassOutputs(reflector, kOutputChannels);
    return reflector;
}

void PhotonMapperDirect::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    // Reset Scene
    mpScene = pScene;

    // Reset all passes and sampling helpers
    mpPhotonAS.reset();
    //mpEmissiveLightSampler.reset();
    //mpRTXDI.reset();
    //mResetScreenTex = true;
    //mChangePhotonLightBufferSize = true;

    //mTracePhotonPass = RayTraceProgramHelper::create();
    //mGenerateInitialSamplesPass = RayTraceProgramHelper::create();
    //mpResampleReservoirFGPass.reset();
    //mpResampleReservoirCausticPass.reset();
    //mpEvaluateReservoirsPass.reset();

    if (mpScene)
    {
        if (mpScene->hasGeometryType(Scene::GeometryType::Custom))
        {
            logWarning("This render pass only supports triangles. Other types of geometry will be ignored.");
        }
    }
}










void PhotonMapperDirect::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // renderData holds the requested resources
    // auto& pTexture = renderData.getTexture("src");

    if (!mpScene)
        return;


}

void PhotonMapperDirect::preparePhotonTrace(RenderContext* pRenderContext, const RenderData& renderData)
{
    //TODO: wird alles im konstruktor getan, einiges sollte aber vlt dynamisch wiederhotl werden können basierend auf bools die notwendigkeit dazu anzeigen, einiges kann auch zu set scene ausgelagert werden
    // alles was ich dachte was vlt nur ein mal on construction getan werden muss is hier drin, nur weniges ist in set scene
    // glücklicher weise muss dieser shi nur ein mal in der cornell box laufen

    mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_UNIFORM);

    auto& pLights = mpScene->getLightCollection(pRenderContext);
    pLights->prepareSyncCPUData(pRenderContext);

    //Photon AS and corresponding data
    mpPhotonAABB = Buffer::createStructured( //gets filled by shaders with AABB that represent bounding boxes with their radius
        mpDevice, sizeof(AABB), mNumMaxPhotons, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        Buffer::CpuAccess::None, nullptr, false);
    mpPhotonAABB->setName("PhotonAABB");
    mpPhotonData = Buffer::createStructured( //contains photon data corresponding to AABB at same index
        mpDevice, sizeof(float) * 12 /*TODO: anpassen auf sowas wie sizeof(photonStruct) wenn ich das mal endlich finde*/, mNumMaxPhotons, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        Buffer::CpuAccess::None, nullptr, false);
    mpPhotonData->setName("PhotonData");

    std::vector<uint64_t> aabbCount = {mNumMaxPhotons}; //my little guy requires his vectors
    std::vector<uint64_t> aabbGPUAddress = {mpPhotonAABB->getGpuAddress()};
    mpPhotonAS = std::make_unique<CustomAccelerationStructure>(
        mpDevice, aabbCount, aabbGPUAddress, CustomAccelerationStructure::BuildMode::FastBuild,
        CustomAccelerationStructure::UpdateMode::TLASOnly);

    //shader library
    RtProgram::Desc desc;
    desc.addShaderModules(mpScene->getShaderModules());
    desc.addShaderLibrary(kShaderTracePhotons);
    desc.setMaxPayloadSize(sizeof(float) * 4 /*change to actual payload size (ka wie groß ich die mache*/);
    desc.setMaxAttributeSize(mpScene->getRaytracingMaxAttributeSize());
    desc.setMaxTraceRecursionDepth(1); //no indirections here, even with indirictions you would just program the raygen shader well
    if (!mpScene->hasProceduralGeometry())
        desc.setPipelineFlags(RtPipelineFlags::SkipProceduralPrimitives);

    mTracePhotonPass = RayTraceProgramHelper::create();

    //binding table (hit groups and how shaders are named)
    mTracePhotonPass.pBindingTable = RtBindingTable::create(1 /*count of miss shaders that get chosen by miss index*/, 1 /*count of ray types that chose hit group*/, mpScene->getGeometryCount());
    auto& sbt = mTracePhotonPass.pBindingTable; //hit groups are just bundles of shaders
    sbt->setRayGen(desc.addRayGen("rayGen", mpScene->getTypeConformances())); //raygen always exactly one, hence outside of hitgroup
    sbt->setMiss(0, desc.addMiss("miss")); //miss determined by assigned miss index, hence outside of hit groups
    if (mpScene->hasGeometryType(Scene::GeometryType::TriangleMesh))
    {
        sbt->setHitGroup(0 /*raytype / hitgroupidx */, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), desc.addHitGroup("closestHit"));
        //bundles of closest-, any-hit and interesectionshaders, get chosen by combination of raytype / hitgroupidx and geometry type
    }

    //scene defines
    DefineList defines; 
    defines.add("USE_EMISSIVE_LIGHT", mpScene->useEmissiveLights() ? "1" : "0"); //always good to know i guess
    defines.add(mpScene->getSceneDefines()); //i hope this wont cause any extra considerations

    mTracePhotonPass.pProgram = RtProgram::create(mpDevice, desc, defines);

    //Photon Mapper specific defines
    mTracePhotonPass.pProgram->addDefine("PHOTON_BUFFER_SIZE", std::to_string(mNumMaxPhotons));
    //mTracePhotonPass.pProgram->addDefine("ROUGHNESS_THRESHOLD", std::to_string(mSpecularRoughnessThreshold)); //no caustic vs global differentiation
    //mTracePhotonPass.pProgram->addDefines(getMaterialDefines()); // TODO: add if needed in shader, fkt to add is lower in restir FG lite

    //shadervariables
    mTracePhotonPass.initProgramVars(mpDevice, mpScene, mpSampleGenerator);
    auto var = mTracePhotonPass.pVars->getRootVar();
    mpScene->setRaytracingShaderData(pRenderContext, var);

    var["CB"]["gFrameCount"] = mFrameCount;
    var["CB"]["gPhotonRadius"] = mPhotonRadius;
    //var["CB"]["gMaxBounces"] = mPhotonMaxBounces; //no bounces
    //var["CB"]["gGlobalRejectionProb"] = mGlobalPhotonRejection; //i wont bother with this honestly
    //var["CB"]["gUseAnalyticLights"] = analyticOnly; // TODO: add if light sampler fkt needs it
    var["CB"]["gDispatchDimension"] = mShaderDispatchDim; // fill buffer up every time, we will see how that works (shoulndt be that bad
                                                          // since raytraced visibility should be much more expensive

    //shader output buffers (funily enough as variables)
    var["gPhotonAABB"] = mpPhotonAABB;
    var["gPhotonData"] = mpPhotonData;
    //no photon counter since buffer gets filled up every time
    //TODO: wenn das wirklich nicht klar geht schauen ob der photon counter das tut was ich denke
}

void PhotonMapperDirect::tracePhotons(RenderContext* pRenderContext, const RenderData& renderData)
{
    //dispatch raytraces (which fill AABB buffer)
    mpScene->raytrace(
        pRenderContext, mTracePhotonPass.pProgram.get(), mTracePhotonPass.pVars, uint3(mShaderDispatchDim, mShaderDispatchDim, 1) //Photon buffer size = dispatchdim**2, so it fills up exactly. 
    ); //TODO: if it doesnt pose scheduling problems, just use (mNumMaxPhotons,1,1), or see the almighty reason in the shader (maybe it gets appearant after seeing the source)

    //update AS with new photons
    std::vector<uint64_t> photonBuildSize = {mNumMaxPhotons}; //ma boy really cant go without his vectors (i will do unspeakable things if this turns into an error)
    mpPhotonAS->update(pRenderContext, photonBuildSize);
}






void PhotonMapperDirect::RayTraceProgramHelper::initProgramVars(
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
