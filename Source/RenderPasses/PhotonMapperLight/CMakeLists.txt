add_plugin(PhotonMapperLight)

target_sources(PhotonMapperLight PRIVATE
    PhotonMapperLight.cpp
    PhotonMapperLight.h
    LightSampler.slang
    TracePhotons.rt.slang
    CollectPhotons.cs.slang
    StructsAndHelpers.slang
)

target_source_group(PhotonMapperLight "RenderPasses")
