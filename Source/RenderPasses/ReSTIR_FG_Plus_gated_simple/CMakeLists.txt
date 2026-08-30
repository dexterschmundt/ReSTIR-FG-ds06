add_plugin(ReSTIR_FG_Plus_gated_simple)

target_sources(ReSTIR_FG_Plus_gated_simple PRIVATE
    ReSTIR_FG_Plus_gated_simple.cpp
    ReSTIR_FG_Plus_gated_simple.h
)

target_source_group(ReSTIR_FG_Plus_gated_simple "RenderPasses")
