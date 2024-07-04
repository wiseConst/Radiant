cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")

# Multithreaded project compilation
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -MP")

include(FetchContent)

set(CMAKE_CONFIGURATION_TYPES Debug Release)
set(PROJECT_NAME TestBed)
project(${PROJECT_NAME} LANGUAGES CXX)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Source)

file(GLOB_RECURSE SRC_FILES "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp")
set(ALL_FILES ${SRC_FILES})

# Automatically group all sources into folders for MVS.
foreach(FILE ${SRC_FILES})
    file(RELATIVE_PATH REL_FILE ${CMAKE_CURRENT_SOURCE_DIR} ${FILE})
    get_filename_component(DIR "${REL_FILE}" DIRECTORY)
    string(REPLACE "/" "\\" GROUP "${DIR}")

    source_group("${GROUP}" FILES ${FILE})
endforeach()

add_executable(${PROJECT_NAME} ${ALL_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR})
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CORE_DIR}/pch.h)

if(MSVC)
    # Dynamic MSVC runtime library linking
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>DLL$<$<NOT:$<CONFIG:Debug>>:>)

    # target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:/SUBSYSTEM:WINDOWS>")
endif()

# ============= ThirdParty =============

# NVIDIA RENDER INTERFACE
FetchContent_Declare(
    NRI
    GIT_REPOSITORY https://github.com/NVIDIAGameWorks/NRI.git
    GIT_TAG        main
    GIT_SHALLOW    true
)
set(NRI_STATIC_LIBRARY ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(NRI)
target_link_libraries(${PROJECT_NAME} PRIVATE NRI)
target_link_libraries(${PROJECT_NAME} PRIVATE NRI_D3D12)
target_link_libraries(${PROJECT_NAME} PRIVATE NRI_VK)
target_link_libraries(${PROJECT_NAME} PRIVATE NRI_Shared)
target_include_directories(${PROJECT_NAME} PRIVATE ${NRI_SOURCE_DIR}/Include)

# glfw
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        master
    GIT_SHALLOW    true
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")

# TODO: Fill imgui
# imgui 
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    true
)
FetchContent_MakeAvailable(imgui)
#target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)
#set_target_properties(imgui::imgui PROPERTIES FOLDER "ThirdParty")

# glm
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        master
    GIT_SHALLOW    true
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
        GIT_TAG        v1.x
        GIT_SHALLOW    true
)
FetchContent_MakeAvailable(spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
set_target_properties(spdlog PROPERTIES FOLDER "ThirdParty")

# fastgltf
FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        main
    GIT_SHALLOW    true
)
FetchContent_MakeAvailable(fastgltf)
target_link_libraries(${PROJECT_NAME} PRIVATE fastgltf)
set_target_properties(fastgltf PROPERTIES FOLDER "ThirdParty")

# meshoptimizer
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        master
    GIT_SHALLOW    true
)
FetchContent_MakeAvailable(meshoptimizer)
target_link_libraries(${PROJECT_NAME} PRIVATE meshoptimizer)
set_target_properties(meshoptimizer PROPERTIES FOLDER "ThirdParty")

# stb_image
FetchContent_Declare(
    stb_image
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    true
)
FetchContent_MakeAvailable(stb_image)
target_include_directories(${PROJECT_NAME} PRIVATE ${stb_image_SOURCE_DIR})

# tracy profiler
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        master
    GIT_SHALLOW    true
)
FetchContent_MakeAvailable(tracy)
target_link_libraries(${PROJECT_NAME} PRIVATE Tracy::TracyClient)
set_target_properties(TracyClient PROPERTIES FOLDER "ThirdParty")

# ankerl's robin_hood backward shift deletion hashmap/set
FetchContent_Declare(
    unordered_dense
    GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
    GIT_TAG        main
    GIT_SHALLOW    true
)
FetchContent_MakeAvailable(unordered_dense)
target_link_libraries(${PROJECT_NAME} PRIVATE unordered_dense)
set_target_properties(unordered_dense PROPERTIES FOLDER "ThirdParty")

# SteamAudio
FetchContent_Declare(
    SteamAudio
    GIT_REPOSITORY https://github.com/ValveSoftware/steam-audio.git
    GIT_TAG        master
    GIT_SHALLOW    true
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(STEAMAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STEAMAUDIO_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(STEAMAUDIO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)   
FetchContent_MakeAvailable(SteamAudio)

#target_link_libraries(${PROJECT_NAME} PRIVATE SteamAudio)
#set_target_properties(SteamAudio PROPERTIES FOLDER "ThirdParty")

add_compile_options($<$<CONFIG:Debug>:-Og>)
add_compile_options($<$<CONFIG:Release>:-O3 -lto>)