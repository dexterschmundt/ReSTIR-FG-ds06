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
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Rendering/AccelerationStructure/CustomAccelerationStructure.h"

using namespace Falcor;

class PhotonMapperLightGated : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(PhotonMapperLightGated, "PhotonMapperLightGated", "Insert pass description here.");

    static ref<PhotonMapperLightGated> create(ref<Device> pDevice, const Properties& props) { return make_ref<PhotonMapperLightGated>(pDevice, props); }

    PhotonMapperLightGated(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override {}
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

private:

    uint mFrameCount = 0;
    // uint2 mScreenRes = uint2(0, 0); //just used renderData.defaultTexSize //TODO: kann sein dass das nicht läuft, dann ändern, kann mir
    // aber nicht vorstellen dass das nötig is da dieser member immer nach defaultTexSize gesetzt wird und eher genutzt wird um änderungen
    // zu erkennen
    ref<Scene> mpScene; // my honest, most sincere reaction when a members name deadass does not start with "mp" (who tf even dares to omit
                        // it) : (⊙︿⊙) (☉Ô☉) (ʘᗩʘ’) ╰། ◉ ◯ ◉ །╯
    ref<SampleGenerator> mpSampleGenerator;

    // Photon Data and AS
    std::unique_ptr<CustomAccelerationStructure> mpPhotonAS;
    ref<Buffer> mpPhotonAABB;
    ref<Buffer> mpPhotonData;

    // Ressources for Resampling
    ref<Buffer> mpCausticReservoir[2]; // Screenspace Reservoir for the Caustic sample Photon, two not because caustic and global Photons,
                                       // but for pingPongRendering for temporal resample
    bool mCanResample = false; // to indicate that second buffer isnt filled on first frame. Could be done with frame count since nothing in
                               // this entire pass is made to ever change anything, but i do it like this for compatibility

    // Photon trace params
    uint mShaderDispatchDim = 256; // sqrt of how much photon paths are generated per frame.
    int mPhotonMaxBounces = 5;
    uint mNumMaxPhotons = mShaderDispatchDim * mShaderDispatchDim * mPhotonMaxBounces; // there is always enough space for all photons,
                                                                                       // mostly not filled up that much because of
                                                                                       // rejection. AS also gets built with actual photons
                                                                                       // because of Photon counter
    float mGlobalPhotonRejection = 0.3f;
    float mPhotonRadius = 0.020f;                 // only one value since no caustic ones used
    float mMixedLightsAnalyticProbability = 0.5f; // i have no clue man
    float mSpecularRoughnessThreshold = 0.25f;

    // Photon Gate params
    float mGateWidth = 0.05f;
    float mFrameToAllowedDistance = (1.0f / 60.0f) * 0.1f; //one world coordinate unit per second assuming 60 frames per second

    // Photon counter stuff (that wont get used because ts doesnt work( wait maybe the decay from photon counter and the accumulation from
    // keeping the AS cancels out???)))
    ref<Buffer> mpPhotonCounter;
    ref<Buffer> mpPhotonCounterCPU;
    uint /*2*/ mCurrentPhotonCount = mNumMaxPhotons;
    float mASBuildBufferPhotonOverestimate = 1.15f;

    bool mUseLambertianDiffuse = true;

    // Resampling stuff
    struct ResamplingSettings
    {
        bool enable = true;
        uint confidenceCap = 10; // Maximum confidence allowed //confidence is the maximum amount of samples a reservoir is allowed to claim
                                 // to combine, the lower its set, the less weight the old samples get and therefore the commitment to old
                                 // samples (weird dots that stay) and also the correlation gets lower (less weight to old samples), but in
                                 // general less samples are kept (less weigh to them) so average is worse
        uint spatialSamples = 0; // Number of spatial samples, none because no spatial resampling
        uint disocclusionBoostExtraSamples = 0; // Number of spatial samples if no temporal surface was found
        float samplingRadius = 20.f;            // Sampling radius in pixel
    };

    ResamplingSettings mResampleSettings = {};
    float mNormalThreshold = 0.6f;

    // stuff that will contain the passes
    struct RayTraceProgramHelper // didnt look that bad so i copied it
    {
        ref<RtProgram> pProgram;
        ref<RtBindingTable> pBindingTable;
        ref<RtProgramVars> pVars;

        static const RayTraceProgramHelper create()
        {
            RayTraceProgramHelper r;
            r.pProgram = nullptr;
            r.pBindingTable = nullptr;
            r.pVars = nullptr;
            return r;
        }

        void initProgramVars(ref<Device> pDevice, ref<Scene> pScene, ref<SampleGenerator> pSampleGenerator);
    };

    RayTraceProgramHelper mTracePhotonPass;
    ref<ComputePass> mCollectPhotonPass;
    ref<ComputePass> mTemporalResamplePass;
    ref<ComputePass> mpFinalizeColorPass;

    // fkts to handle tasks/execute tasks

    void PhotonMapperLightGated::handlePhotonCounter(RenderContext* pRenderContext);

    void PhotonMapperLightGated::prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    // does everything that felt like it needs to only be done on construction, will put it in the loop too tough (so it gets reeeaaallly
    // inefficient (probably))

    void PhotonMapperLightGated::tracePhotons(RenderContext* pRenderContext, const RenderData& renderData); // life without these two parameters
                                                                                                       // like "...hmmm, som'thin is
                                                                                                       // missin, ma boy" (i should

    void PhotonMapperLightGated::collectPhotons(RenderContext* pRenderContext, const RenderData& renderData);

    void PhotonMapperLightGated::TemporalResampling(RenderContext* pRenderContext, const RenderData& renderData);

    void PhotonMapperLightGated::FinalizeColors(RenderContext* pRenderContext, const RenderData& renderData);

    DefineList PhotonMapperLightGated::getMaterialDefines();
};
