add_plugin(PhotonMapperDirect)

target_sources(PhotonMapperDirect PRIVATE
    PhotonMapperDirect.cpp
    PhotonMapperDirect.h
    LightSampler.slang
    TracePhotons.rt.slang
)

target_source_group(PhotonMapperDirect "RenderPasses")
