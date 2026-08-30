add_plugin(ReSTIR_FG_Plus_gated_simple)

target_sources(ReSTIR_FG_Plus_gated_simple PRIVATE
    ReSTIR_FG_Plus_gated_simple.cpp
    ReSTIR_FG_Plus_gated_simple.h

    StructsAndHelpers.slang
    NextEventEstimationHelper.slang

    TracePhotons.rt.slang
    TraceCamera.rt.slang
    BackprojectCaustics.cs.slang
   
    ResamplePathReservoir.cs.slang
    ResampleReservoirCaustic.cs.slang
	EvaluateReservoirs.cs.slang
)

target_copy_shaders(ReSTIR_FG_Plus_gated_simple RenderPasses/ReSTIR_FG_Plus_gated_simple)
target_source_group(ReSTIR_FG_Plus_gated_simple "RenderPasses")
