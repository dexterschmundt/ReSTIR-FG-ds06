add_plugin(PhotonMapperLightGated)

target_sources(PhotonMapperLightGated PRIVATE
    PhotonMapperLightGated.cpp
    PhotonMapperLightGated.h
    LightSampler.slang
    TracePhotons.rt.slang
    CollectPhotons.cs.slang
    StructsAndHelpers.slang
    TemporalResampling.cs.slang
    FinalizeColors.cs.slang
)

target_source_group(PhotonMapperLightGated "RenderPasses")
