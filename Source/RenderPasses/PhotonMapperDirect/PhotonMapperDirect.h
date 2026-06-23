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
#include "Rendering/AccelerationStructure/CustomAccelerationStructure.h"

using namespace Falcor;

class PhotonMapperDirect : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(PhotonMapperDirect, "PhotonMapperDirect", "Insert pass description here.");

    static ref<PhotonMapperDirect> create(ref<Device> pDevice, const Properties& props) { return make_ref<PhotonMapperDirect>(pDevice, props); }

    PhotonMapperDirect(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override {}
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

    

private:
    uint mShaderDispatchDim = 32; //dsipatch(32,32,1) i have no clue yet why invocation index gets done qudratic. DispatchDim gets computed in FG_lite, here implemented as member
    uint mNumMaxPhotons = mShaderDispatchDim * mShaderDispatchDim; /* TODO vlt irwann mal im konstruktor setzen*/ // photon buffer gets filled up every time, so quadratic dispatch matches photon buffer size
    uint mFrameCount = 0;
    //uint2 mScreenRes = uint2(0, 0); //just used renderData.defaultTexSize //TODO: kann sein dass das nicht läuft, dann ändern, kann mir aber nicht vorstellen dass das nötig is da dieser member immer nach defaultTexSize gesetzt wird und eher genutzt wird um änderungen zu erkennen

    float mPhotonRadius = 0.020f; // only one value since no caustic ones used

    float mMixedLightsAnalyticProbability = 0.5f; // i have no clue man

    std::unique_ptr<CustomAccelerationStructure> mpPhotonAS;
    ref<Buffer> mpPhotonAABB;
    ref<Buffer> mpPhotonData;

    ref<Scene> mpScene;                     //my honest, most sincere reaction when a members name deadass does not start with "mp" (who tf even dares to omit it) : (⊙︿⊙) (☉Ô☉) (ʘᗩʘ’) ╰། ◉ ◯ ◉ །╯
    ref<SampleGenerator> mpSampleGenerator;

    


    struct RayTraceProgramHelper //didnt look that bad so i copied it
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

    void PhotonMapperDirect::prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    //does everything that felt like it needs to only be done on construction, will put it in the loop too tough (so it gets reeeaaallly inefficient (probably))

    void PhotonMapperDirect::tracePhotons(RenderContext* pRenderContext, const RenderData& renderData); //life without these two parameters like "...hmmm, som'thin is missin, ma boy" (i should implement these as lambdas at some point)
    //looping execution of photon tracing

    void PhotonMapperDirect::collectPhotons(RenderContext* pRenderContext, const RenderData& renderData);
    // looping execution of photon tracing
};
//TODO: in radianceEstimatePass dann mit visibilitybuffer positionen auslesen, von dort durch die PhotonAS tracen (mit sehr kurzem strahl) und mit anyhit in raypayload aufsummieren (und durch fläche teilen im raygen)
//außerdem photon count erhöhen auf so ne mille

//worauf das hier verzichtet:
//  indirektionen für photonen
//  final gather
//  caustic vs global
//  wiederverwenden von photonen
//  alpha test
