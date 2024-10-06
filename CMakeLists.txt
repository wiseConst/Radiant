cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
include(FetchContent)

# NOTE: C++23 for now used with std::move_only_function
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CONFIGURATION_TYPES Debug Release)
set(PROJECT_NAME Radiant)
project(${PROJECT_NAME} LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")

if (MSVC)
    # This will enable INTERPROCEDURAL_OPTIMIZATION for all targets created after this line. Those created before are not affected.
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

    # Multithreaded project compilation using MSVC
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

# Set the output directory for executables based on configuration type
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Binaries/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Binaries/Release)

# Set the output directory for static libraries based on configuration type
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Binaries/Debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Binaries/Release)

# Set the output directory for shared libraries based on configuration type
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Binaries/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Binaries/Release)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Source)
set(SHADERS_DIR ${CMAKE_SOURCE_DIR}/Assets/Shaders)

file(GLOB_RECURSE SRC_FILES "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.cppm" "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp") 
set(ALL_FILES ${SRC_FILES})

# Automatically group all sources into folders for MVS.
foreach (FILE ${SRC_FILES})
    file(RELATIVE_PATH REL_FILE ${CMAKE_CURRENT_SOURCE_DIR} ${FILE})
    get_filename_component(DIR "${REL_FILE}" DIRECTORY)
    string(REPLACE "/" "\\" GROUP "${DIR}")

    source_group("${GROUP}" FILES ${FILE})
endforeach()

add_executable(${PROJECT_NAME} ${ALL_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR})
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CORE_DIR}/pch.hpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${SHADERS_DIR})

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    $<$<CONFIG:Debug>:RDNT_DEBUG=1 RDNT_RELEASE=0>
    $<$<CONFIG:Release>:RDNT_RELEASE=1 RDNT_DEBUG=0>
)

if (MSVC)
    # Dynamic MSVC runtime library linking
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>DLL$<$<NOT:$<CONFIG:Debug>>:>)

    # No console
    # target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:/SUBSYSTEM:WINDOWS>")
endif()

# ============= ThirdParty =============

# NVTT (texture compression NV cards only)
set(NVTT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/nvtt")
target_include_directories(${PROJECT_NAME} PRIVATE ${NVTT_ROOT}/include)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${NVTT_ROOT}/lib/nvtt30205.lib)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
         ${NVTT_ROOT}/bin/ ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
         ${NVTT_ROOT}/bin/ ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}
    )
endif()

# graphviz
set(GRAPHVIZ_VERSION 12.1.0)
set(GRAPHVIZ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/graphviz)
if (NOT EXISTS ${GRAPHVIZ_ROOT})
    message(STATUS "Downloading graphviz...")
    set(GRAPHVIZ_URL "https://gitlab.com/api/v4/projects/4207231/packages/generic/graphviz-releases/${GRAPHVIZ_VERSION}/windows_10_cmake_Release_Graphviz-${GRAPHVIZ_VERSION}")
    if (WIN32)
        file(DOWNLOAD ${GRAPHVIZ_URL}-win64.zip ${GRAPHVIZ_ROOT}.zip)
    elseif(UNIX)
        message("Graphviz downloading for other than windows systems isn't supported for now!")
    elseif(APPLE)
        message("Graphviz downloading for other than windows systems isn't supported for now!")
    endif()

    message(STATUS "Extracting graphviz...")
    file(ARCHIVE_EXTRACT INPUT ${GRAPHVIZ_ROOT}.zip DESTINATION ${GRAPHVIZ_ROOT})
    file(REMOVE ${GRAPHVIZ_ROOT}.zip)
endif()

# slang - modern feature-rich, cross-gfx-api, robust shader language
set(SLANG_VERSION 2024.13)
set(SLANG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/slang)
if (NOT EXISTS ${SLANG_ROOT})
    message(STATUS "Downloading slang...")
    set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}")
    if (WIN32)
        file(DOWNLOAD ${SLANG_URL}-windows-x86_64.zip ${SLANG_ROOT}.zip)
    elseif(UNIX)
        file(DOWNLOAD ${SLANG_URL}-linux-x86_64.zip ${SLANG_ROOT}.zip)
    elseif(APPLE)
        file(DOWNLOAD ${SLANG_URL}-macos-x86_64.zip ${SLANG_ROOT}.zip)
    endif()

    message(STATUS "Extracting slang...")
    file(ARCHIVE_EXTRACT INPUT ${SLANG_ROOT}.zip DESTINATION ${SLANG_ROOT})
    file(REMOVE ${SLANG_ROOT}.zip)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE ${SLANG_ROOT}/include)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SLANG_ROOT}/lib/slang.lib)
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
     ${SLANG_ROOT}/bin/ ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
     ${SLANG_ROOT}/bin/ ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}
)

# Vulkan SDK
message(STATUS "Looking for Vulkan SDK...")
find_package(Vulkan REQUIRED)
target_compile_definitions(${PROJECT_NAME} PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS}/vma)

# glfw
message(STATUS "Fetching GLFW...")
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG master
        GIT_SHALLOW true
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
set_target_properties(glfw PROPERTIES FOLDER "ThirdParty/glfw")
set_target_properties(uninstall PROPERTIES FOLDER "ThirdParty/glfw")
set_target_properties(update_mappings PROPERTIES FOLDER "ThirdParty/glfw")

# imgui 
message(STATUS "Fetching imgui...")
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG master
        GIT_SHALLOW true
)

FetchContent_MakeAvailable(imgui)
target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

# Manually creating imgui lib since it hasn't CMakeLists.txt (what a pity...)
file(GLOB IMGUI_SOURCES ${imgui_SOURCE_DIR}/*.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_include_directories(imgui PRIVATE ${imgui_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS} ${glfw_SOURCE_DIR}/include)

target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

# glm
message(STATUS "Fetching glm...")
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG master
        GIT_SHALLOW true
)

set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
set(GLM_ENABLE_FAST_MATH ON CACHE BOOL "" FORCE)

set(GLM_ENABLE_SIMD_AVX ON CACHE BOOL "" FORCE)
set(GLM_ENABLE_SIMD_AVX2 ON CACHE BOOL "" FORCE)
set(GLM_TEST_ENABLE_SIMD_NEON ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glm)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
set_target_properties(glm PROPERTIES FOLDER "ThirdParty")

# spdlog
message(STATUS "Fetching spdlog...")
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.x
        GIT_SHALLOW true
)

set(SPDLOG_ENABLE_PCH ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
set_target_properties(spdlog PROPERTIES FOLDER "ThirdParty")

# fastgltf
message(STATUS "Fetching fastgltf...")
FetchContent_Declare(
        fastgltf
        GIT_REPOSITORY https://github.com/spnda/fastgltf.git
        GIT_TAG main
        GIT_SHALLOW true
)
FetchContent_MakeAvailable(fastgltf)
target_link_libraries(${PROJECT_NAME} PRIVATE fastgltf)
set_target_properties(fastgltf PROPERTIES FOLDER "ThirdParty")

# meshoptimizer
message(STATUS "Fetching meshoptimizer...")
FetchContent_Declare(
        meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG master
        GIT_SHALLOW true
)
FetchContent_MakeAvailable(meshoptimizer)
target_link_libraries(${PROJECT_NAME} PRIVATE meshoptimizer)
set_target_properties(meshoptimizer PROPERTIES FOLDER "ThirdParty")

# stb_image
message(STATUS "Fetching stb_image...")
FetchContent_Declare(
        stb_image
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW true
)
FetchContent_MakeAvailable(stb_image)
target_include_directories(${PROJECT_NAME} PRIVATE ${stb_image_SOURCE_DIR})

# ankerl's robin_hood backward shift deletion hashmap/set
message(STATUS "Fetching unordered_dense...")
FetchContent_Declare(
        unordered_dense
        GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
        GIT_TAG main
        GIT_SHALLOW true
)
FetchContent_MakeAvailable(unordered_dense)
target_link_libraries(${PROJECT_NAME} PRIVATE unordered_dense)
set_target_properties(unordered_dense PROPERTIES FOLDER "ThirdParty")

# TODO: Include Jolt

## When turning this option on, the library will be compiled using doubles for positions. This allows for much bigger worlds.
#set(DOUBLE_PRECISION OFF)
#
## When turning this option on, the library will be compiled with debug symbols
#set(GENERATE_DEBUG_SYMBOLS ON)
#
## When turning this option on, the library will be compiled in such a way to attempt to keep the simulation deterministic across platforms
#set(CROSS_PLATFORM_DETERMINISTIC OFF)
#
## When turning this option on, the library will be compiled with interprocedural optimizations enabled, also known as link-time optimizations or link-time code generation.
## Note that if you turn this on you need to use SET_INTERPROCEDURAL_OPTIMIZATION() or set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON) to enable LTO specificly for your own project as well.
## If you don't do this you may get an error: /usr/bin/ld: libJolt.a: error adding symbols: file format not recognized
#if (MSVC)
#    set(INTERPROCEDURAL_OPTIMIZATION ON)
#endif ()
#
## When turning this on, in Debug and Release mode, the library will emit extra code to ensure that the 4th component of a 3-vector is kept the same as the 3rd component 
## and will enable floating point exceptions during simulation to detect divisions by zero. 
## Note that this currently only works using MSVC. Clang turns Float2 into a SIMD vector sometimes causing floating point exceptions (the option is ignored).
#set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
#
## Number of bits to use in ObjectLayer. Can be 16 or 32.
#set(OBJECT_LAYER_BITS 16)
#
## Select X86 processor features to use, by default the library compiles with AVX2, if everything is off it will be SSE2 compatible.
#set(USE_SSE4_1 ON)
#set(USE_SSE4_2 ON)
#set(USE_AVX ON)
#set(USE_AVX2 ON)
#set(USE_AVX512 OFF)
#set(USE_LZCNT ON)
#set(USE_TZCNT ON)
#set(USE_F16C ON)
#set(USE_FMADD ON)
#
#FetchContent_Declare(
#        JoltPhysics
#        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics
#        GIT_TAG master
#        SOURCE_SUBDIR "Build"
#)
#FetchContent_MakeAvailable(JoltPhysics)
#target_link_libraries(${PROJECT_NAME} PRIVATE Jolt)
#set_target_properties(Jolt PROPERTIES FOLDER "ThirdParty")

# TODO: Valve's GameNetworkingSockets
#FetchContent_Declare(
#    GameNetworkingSockets
#    GIT_REPOSITORY https://github.com/ValveSoftware/GameNetworkingSockets.git
#    GIT_TAG        master
#    GIT_SHALLOW    true
#)
#FetchContent_MakeAvailable(GameNetworkingSockets)
#target_link_libraries(${PROJECT_NAME} PRIVATE GameNetworkingSockets)
#set_target_properties(GameNetworkingSockets PROPERTIES FOLDER "ThirdParty")

# TODO: SteamAudio
#FetchContent_Declare(
#    SteamAudio
#    GIT_REPOSITORY https://github.com/ValveSoftware/steam-audio.git
#    GIT_TAG        master
#    GIT_SHALLOW    true
#)

#set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
#set(STEAMAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
#set(STEAMAUDIO_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
#set(STEAMAUDIO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)   
#FetchContent_MakeAvailable(SteamAudio)

#target_link_libraries(${PROJECT_NAME} PRIVATE SteamAudio)
#set_target_properties(SteamAudio PROPERTIES FOLDER "ThirdParty")

# openmp
message(STATUS "Looking for openmp...")
find_package(OpenMP REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
if (OpenMP_CXX_FOUND)
    if (MSVC)
        set(OpenMP_CXX_FLAGS "/openmp")
    else ()
        set(OpenMP_CXX_FLAGS "-fopenmp")
    endif ()

    target_compile_options(${PROJECT_NAME} PRIVATE ${OpenMP_CXX_FLAGS})
endif ()

add_compile_options($<$<CONFIG:Debug>:-Og>)
add_compile_options($<$<CONFIG:Release>:-O3 -lto>)

set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")
add_compile_options(-H) # Print all files that will be precompiled
add_compile_options(-ftime-report) # Print overall stats
