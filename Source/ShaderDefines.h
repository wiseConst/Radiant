#pragma once

#ifdef __cplusplus

using float4x4 = glm::mat4;
using float3   = glm::vec3;
using float4   = glm::vec4;

#endif

struct CameraData
{
    float4x4 ProjectionMatrix;
    float4x4 ViewMatrix;
    float4x4 ViewProjectionMatrix;
    float3 Position;
    float zNear;
    float zFar;
};

struct VertexPosition
{
    float3 Position;
};

struct VertexAttribute
{
    float3 Normal;
    float4 Color;
};

namespace Shaders
{

    static const uint32_t s_MAX_BINDLESS_IMAGES   = 1 << 16;
    static const uint32_t s_MAX_BINDLESS_TEXTURES = 1 << 16;
    static const uint32_t s_MAX_BINDLESS_SAMPLERS = 1 << 11;

#ifndef __cplusplus

    [vk::binding(0, 0)] RWTexture2D<float4> RWImage2D_Heap[];
    [vk::binding(0, 1)] Texture2D Image2D_Heap[];
    [vk::binding(0, 2)] SamplerState Sampler_Heap[];

#endif

}  // namespace Shaders
