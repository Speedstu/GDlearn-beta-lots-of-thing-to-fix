# gdlearn2 - works with MSVC, clang and gcc. No external dependencies.
cmake_minimum_required(VERSION 3.16)
project(gdlearn2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(WARNING "32-bit build detected. The beam search needs more memory "
                  "than a 32-bit process can address (~2 GB), so wide beams "
                  "will die mid-run. Reconfigure with: cmake -B build -A x64")
endif()

file(GLOB_RECURSE GDLEARN_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(gdlearn ${GDLEARN_SOURCES})
target_include_directories(gdlearn PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(gdlearn PRIVATE /O2 /fp:fast /W3 /EHsc)
  # MSVC defaults to a 1 MB stack per thread; Linux gives 8 MB. Match it.
  target_link_options(gdlearn PRIVATE /STACK:8388608)
else()
  target_compile_options(gdlearn PRIVATE -O3 -ffast-math -fno-finite-math-only -Wall -Wextra -Wno-unused-parameter)
endif()

find_package(Threads REQUIRED)
target_link_libraries(gdlearn PRIVATE Threads::Threads)

# Optional: -DGDLEARN_NATIVE=ON for -march=native (faster, less portable)
option(GDLEARN_NATIVE "Optimise for the build machine" OFF)
if(GDLEARN_NATIVE AND NOT MSVC)
  target_compile_options(gdlearn PRIVATE -march=native)
endif()
