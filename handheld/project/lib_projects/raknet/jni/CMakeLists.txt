FILE(GLOB raknetCPPSources ./RaknetSources/*.cpp)
set(CMAKE_C_FLAGS "")
set(CMAKE_CXX_FLAGS "")
add_library(raknet ${raknetCPPSources})
