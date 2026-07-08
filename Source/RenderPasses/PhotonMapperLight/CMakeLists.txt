add_plugin(PhotonMapperLight)

target_sources(PhotonMapperLight PRIVATE
    PhotonMapperLight.cpp
    PhotonMapperLight.h
)

target_source_group(PhotonMapperLight "RenderPasses")
