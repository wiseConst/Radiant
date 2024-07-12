cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# NOTE: C++23 for now used with std::move_only_function
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")

# Multithreaded project compilation
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -MP")

include(FetchContent)

set(CMAKE_CONFIGURATION_TYPES Debug Release)
set(PROJECT_NAME Radiant)
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
target_include_directories(${PROJECT_NAME} PRIVATE $ENV{VK_SDK_PATH}/Include $ENV{VK_SDK_PATH}/Include/Volk)

# Detect the platform for appropriate volk lib.
if(WIN32)
    set(VOLK_LIBRARIES $ENV{VK_SDK_PATH}/Lib/volk.lib)
elseif(UNIX AND NOT APPLE)
    set(VOLK_LIBRARIES $ENV{VK_SDK_PATH}/Lib/libvolk.so)
elseif(APPLE)
    set(VOLK_LIBRARIES $ENV{VK_SDK_PATH}/Lib/libvolk.dylib)
else()
    message(FATAL_ERROR "Unsupported platform!")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${VOLK_LIBRARIES})

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

# imgui 
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    true
)

FetchContent_MakeAvailable(imgui)
target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

# Manually creating imgui lib since it hasn't CMakeLists.txt (what a pity...)
file(GLOB IMGUI_SOURCES ${imgui_SOURCE_DIR}/*.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES IMGUI_IMPL_VULKAN_USE_VOLK)
target_include_directories(imgui PRIVATE ${imgui_SOURCE_DIR} $ENV{VK_SDK_PATH}/Include $ENV{VK_SDK_PATH}/Include/Volk ${glfw_SOURCE_DIR}/include)

target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

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
if(OpenMP_CXX_FOUND)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(OpenMP_CXX_FLAGS "/openmp")
    else()
        set(OpenMP_CXX_FLAGS "-fopenmp")
    endif()

    target_compile_options(${PROJECT_NAME} PRIVATE ${OpenMP_CXX_FLAGS})
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    $<$<CONFIG:Debug>:RDNT_DEBUG>
    $<$<CONFIG:Release>:RDNT_RELEASE>
)

add_compile_options($<$<CONFIG:Debug>:-Og>)
add_compile_options($<$<CONFIG:Release>:-O3 -lto>)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")
add_compile_options(-H) # Print all files that will be precompiled
add_compile_options(-ftime-report) # Print overall stats