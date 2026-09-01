
include_directories(. ../.. ../cpu ../includes) 

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-write-strings")

add_library(SoftFloat
    softfloat.c softfloat_decimal.c softfloat_fpsp.c 
)
