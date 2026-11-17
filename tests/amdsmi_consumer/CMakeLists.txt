# Stand-in for AMD SMI (rocm-systems/projects/amdsmi): adds addc-base with
# add_subdirectory and wraps it in an amdsmi_* C function inside a shared
# library whose version script exports only amdsmi_*. CI builds it with
# ADDC_LIB_STATIC=OFF (separate libaddc_lib.so) and ON (built in).
cmake_minimum_required(VERSION 3.20)
project(addc_amdsmi_consumer C CXX)

# AMD SMI's global settings.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion -Wcast-align")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat=2 -fno-common -Wstrict-overflow")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=format-overflow")

set(ADDC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "addc-base source tree")
add_subdirectory("${ADDC_SOURCE_DIR}" addc-base)

add_library(amd_smi SHARED amd_smi.cpp)
target_link_libraries(amd_smi PRIVATE addc::addc_lib)
target_link_options(amd_smi PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/amd_smi.map")

add_executable(amd_smi_cli main.c)
target_link_libraries(amd_smi_cli PRIVATE amd_smi)
