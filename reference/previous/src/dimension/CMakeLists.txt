
include_directories(. ../.. ../debug ../includes ../gui-sdl ${SDL3_INCLUDE_DIRS}) 

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-write-strings")

add_library(Dimension
    dimension.cpp nd_mem.cpp nd_nbic.cpp nd_devs.cpp nd_vio.cpp nd_rom.cpp
    i860.cpp i860dis.cpp nd_sdl.cpp
)

target_link_libraries(Dimension PRIVATE ${SDL3_LIBRARIES})
