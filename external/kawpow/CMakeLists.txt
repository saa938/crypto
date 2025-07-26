cmake_minimum_required(VERSION 3.5)

project(kawpow)

# KawPow library - GPU-optimized PoW algorithm for Monero
# Based on ProgPoW/KawPow algorithm used by Ravencoin

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detect architecture
if(NOT ARCH_ID)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/TargetArch.cmake)
  target_architecture(ARCH_ID)
endif()

# KawPow headers
set(kawpow_headers
    kawpow.h
)

# KawPow sources 
set(kawpow_sources
    kawpow.cpp
)

add_library(kawpow ${kawpow_sources} ${kawpow_headers})

target_include_directories(kawpow
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:.>
)

# Set properties for better optimization
set_target_properties(kawpow PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kawpow PRIVATE
        -O3
        -march=native
        -fPIC
    )
endif()

if(MSVC)
    target_compile_options(kawpow PRIVATE
        /O2
        /arch:AVX2
    )
endif()

# Export the target
install(TARGETS kawpow
    EXPORT kawpowTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES ${kawpow_headers}
    DESTINATION include
)

install(EXPORT kawpowTargets
    FILE kawpowTargets.cmake
    DESTINATION lib/cmake/kawpow
)