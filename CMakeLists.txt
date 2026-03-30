cmake_minimum_required(VERSION 3.18)
project(GDLearnCPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Aggressive optimization for Release builds
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob3 /GL /fp:precise /arch:AVX2 /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# ============================================================================
# LibTorch (optional) - PPO with real neural nets
# Download from: https://pytorch.org/get-started/locally/
# Select: LibTorch / C++ / CPU or CUDA / Windows
# cmake -DTORCH_DIR=G:/libtorch ..
# ============================================================================
if(NOT DEFINED TORCH_DIR)
    set(TORCH_DIR "" CACHE PATH "Path to LibTorch installation")
endif()

if(TORCH_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${TORCH_DIR}")
    find_package(Torch REQUIRED)
    set(HAS_LIBTORCH TRUE)
    message(STATUS "LibTorch found at ${TORCH_DIR}")
else()
    set(HAS_LIBTORCH FALSE)
    message(STATUS "TORCH_DIR not set -> using built-in simple neural net (no dependency)")
endif()

# ============================================================================
# Sources
# ============================================================================
set(SOURCES
    src/main.cpp
    src/memory_reader.cpp
    src/environment.cpp
    src/rewards.cpp
    src/neural_net.cpp
    src/ppo_agent.cpp
    src/input_injector.cpp
    src/trainer.cpp
    src/logger.cpp
    src/level_parser.cpp
    src/simulator.cpp
    src/renderer.cpp
    src/hard_levels.cpp
    src/realistic_levels.cpp
    src/infinite_trainer.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)

if(HAS_LIBTORCH)
    target_link_libraries(${PROJECT_NAME} PRIVATE "${TORCH_LIBRARIES}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_LIBTORCH)
endif()

# Windows APIs for memory reading + input injection + compression
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE psapi Cabinet)
endif()

# Copy LibTorch DLLs next to exe
if(HAS_LIBTORCH AND MSVC)
    file(GLOB TORCH_DLLS "${TORCH_DIR}/lib/*.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TORCH_DLLS} $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
