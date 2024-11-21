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
set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")

# To fix fmt encodings issue.
add_compile_options(/utf-8)

# Multithreaded project compilation using MSVC
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

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

file(GLOB_RECURSE SRC_FILES "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.cppm" "${CORE_DIR}/*.h" "${CORE_DIR}/*.hpp" "${SHADERS_DIR}/*.hpp" "${SHADERS_DIR}/*.h" "${SHADERS_DIR}/*.slang") 
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

# This will enable INTERPROCEDURAL_OPTIMIZATION for all targets created after this line.
set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)

if (MSVC)
    # Dynamic MSVC runtime library linking
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>DLL$<$<NOT:$<CONFIG:Debug>>:>)

    # No console
    # target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:/SUBSYSTEM:WINDOWS>")
endif()

# ============= ThirdParty =============

# NVTT (BCn texture compression)
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
set(SLANG_VERSION 2024.14.5)
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
    
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${SLANG_ROOT}/bin/slang.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}/slang.dll
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${SLANG_ROOT}/bin/slang-glslang.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}/slang-glslang.dll
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${SLANG_ROOT}/bin/slang.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}/slang.dll
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${SLANG_ROOT}/bin/slang-glslang.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}/slang-glslang.dll
	)

endif()

# Vulkan SDK
message(STATUS "Looking for Vulkan SDK...")
find_package(Vulkan REQUIRED)
target_compile_definitions(${PROJECT_NAME} PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

# Vulkan Memory Allocator
set(VMA_VERSION 3.1.0)
set(VMA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/vma)
if (NOT EXISTS ${VMA_ROOT})
    message(STATUS "Downloading Vulkan Memory Allocator...")
    set(VMA_URL "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v${VMA_VERSION}")
    if (WIN32)
        file(DOWNLOAD ${VMA_URL}.zip ${VMA_ROOT}.zip)
        message(STATUS "Extracting Vulkan Memory Allocator...")
        file(ARCHIVE_EXTRACT INPUT ${VMA_ROOT}.zip DESTINATION ${VMA_ROOT})
        file(REMOVE ${VMA_ROOT}.zip)
    elseif(UNIX)
        file(DOWNLOAD ${VMA_URL}.tar.gz ${VMA_ROOT}.tar.gz)
        message(STATUS "Extracting Vulkan Memory Allocator...")
        file(ARCHIVE_EXTRACT INPUT ${VMA_ROOT}.tar.gz DESTINATION ${VMA_ROOT})
        file(REMOVE ${VMA_ROOT}.tar.gz)
    elseif(APPLE)
        message("Vulkan Memory Allocator downloading for APPLE systems isn't supported for now!")
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE "${VMA_ROOT}/VulkanMemoryAllocator-${VMA_VERSION}/include")

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

# openmp
message(STATUS "Looking for openmp...")
find_package(OpenMP REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
if(OpenMP_CXX_FOUND)
    if(MSVC)
        set(OpenMP_CXX_FLAGS "/openmp")
    else()
        set(OpenMP_CXX_FLAGS "-fopenmp")
    endif()

    target_compile_options(${PROJECT_NAME} PRIVATE ${OpenMP_CXX_FLAGS})
endif()

add_compile_options($<$<CONFIG:Debug>:-Og>)
add_compile_options($<$<CONFIG:Release>:-O3 -lto>)
add_compile_options(-H) # Print all files that will be precompiled
add_compile_options(-ftime-report) # Print overall stats

if(MSVC)
    #target_compile_options(${PROJECT_NAME} PRIVATE /LTCG)
    target_link_options(${PROJECT_NAME} PRIVATE /LTCG /INCREMENTAL)
endif()