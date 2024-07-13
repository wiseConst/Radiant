cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# NOTE: C++23 for now used with std::move_only_function
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")

# This will enable INTERPROCEDURAL_OPTIMIZATION for all targets created after this line. Those created before are not affected.
if (MSVC)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# Multithreaded project compilation using MSVC
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -MP")
endif()

include(FetchContent)

set(CMAKE_CONFIGURATION_TYPES Debug Release)
set(PROJECT_NAME Radiant)
project(${PROJECT_NAME} LANGUAGES CXX)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Source)

file(GLOB_RECURSE SRC_FILES "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp")
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
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CORE_DIR}/pch.h)

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    $<$<CONFIG:Debug>:RDNT_DEBUG=1 RDNT_RELEASE=0>
    $<$<CONFIG:Release>:RDNT_RELEASE=1 RDNT_DEBUG=0>
)

if (MSVC)
    # Dynamic MSVC runtime library linking
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>DLL$<$<NOT:$<CONFIG:Debug>>:>)

    # target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:/SUBSYSTEM:WINDOWS>")
endif()

# ============= ThirdParty =============

# TODO: slang - modern feature-rich, cross-gfx-api, robust shader language
#FetchContent_Declare(
#    slang    
#    GIT_REPOSITORY https://github.com/shader-slang/slang.git
#    GIT_TAG        master
#    GIT_SHALLOW    true
#)

#set(SLANG_ENABLE_SLANG_GLSLANG ON CACHE BOOL "" FORCE)
#set(SLANG_ENABLE_TESTS ON CACHE BOOL "" FORCE)
#set(SLANG_LIB_TYPE STATIC CACHE BOOL "" FORCE)

#FetchContent_MakeAvailable(slang)
#target_link_libraries(${PROJECT_NAME} PRIVATE slang)
#set_target_properties(slang PROPERTIES FOLDER "ThirdParty/slang")
#set_target_properties(slang-rt PROPERTIES FOLDER "ThirdParty/slang")
#set_target_properties(slangc PROPERTIES FOLDER "ThirdParty/slang")
#set_target_properties(slangd PROPERTIES FOLDER "ThirdParty/slang")
#set_target_properties(platform PROPERTIES FOLDER "ThirdParty/slang")
#set_target_properties(core PROPERTIES FOLDER "ThirdParty/slang")
#set_target_properties(compiler-core PROPERTIES FOLDER "ThirdParty/slang")
#set_target_properties(copy-prebuilt-binaries PROPERTIES FOLDER "ThirdParty/slang")

# Vulkan SDK
find_package(Vulkan REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE $ENV{VK_SDK_PATH}/Include)

# glfw
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
set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")

# imgui 
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG docking
        GIT_SHALLOW true
)

FetchContent_MakeAvailable(imgui)
target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

# Manually creating imgui lib since it hasn't CMakeLists.txt (what a pity...)
file(GLOB IMGUI_SOURCES ${imgui_SOURCE_DIR}/*.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_include_directories(imgui PRIVATE ${imgui_SOURCE_DIR} $ENV{VK_SDK_PATH}/Include ${glfw_SOURCE_DIR}/include)

target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

# glm
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG master
        GIT_SHALLOW true
)

set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
set(GLM_ENABLE_FAST_MATH ON CACHE BOOL "" FORCE)

# Intrinsics
set(GLM_ENABLE_SIMD_AVX ON CACHE BOOL "" FORCE)
set(GLM_ENABLE_SIMD_AVX2 ON CACHE BOOL "" FORCE)
set(GLM_TEST_ENABLE_SIMD_NEON ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glm)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
set_target_properties(glm PROPERTIES FOLDER "ThirdParty")

# spdlog
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.x
        GIT_SHALLOW true
)
FetchContent_MakeAvailable(spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
set_target_properties(spdlog PROPERTIES FOLDER "ThirdParty")

# fastgltf
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
FetchContent_Declare(
        stb_image
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW true
)
FetchContent_MakeAvailable(stb_image)
target_include_directories(${PROJECT_NAME} PRIVATE ${stb_image_SOURCE_DIR})

# tracy profiler
FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG master
        GIT_SHALLOW true
)
FetchContent_MakeAvailable(tracy)
target_link_libraries(${PROJECT_NAME} PRIVATE Tracy::TracyClient)
set_target_properties(TracyClient PROPERTIES FOLDER "ThirdParty")

# ankerl's robin_hood backward shift deletion hashmap/set
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
