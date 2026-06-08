add_plugin(PhotonMapperDirect)

target_sources(PhotonMapperDirect PRIVATE
    PhotonMapperDirect.cpp
    PhotonMapperDirect.h
    LightSampler.slang
)

target_source_group(PhotonMapperDirect "RenderPasses")
