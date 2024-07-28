#pragma once

#ifdef __cplusplus

using float4x4 = glm::mat4;
using float2   = glm::vec2;
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
    float4 Color;
    float3 Normal;
    float4 Tangent;
    float2 UV;
};

struct Sphere
{
    float3 Origin;
    float Radius;
};

namespace Shaders
{

    static const float s_KINDA_SMALL_NUMBER = 10.E-4f;

    struct DirectionalLight
    {
        float3 Direction;
        float3 Color;
        float Intensity;
        bool bCastShadows;
    };

    // NOTE: By default GfxContext creates white texture, so each texture ID has 0 index!
    struct GLTFMaterial
    {
        struct PBRData
        {
            float4 BaseColorFactor /*{1.f}*/;
            float MetallicFactor /*{1.f}*/;
            float RoughnessFactor /*{1.f}*/;
            uint32_t AlbedoTextureID /*{0}*/;
            uint32_t MetallicRoughnessTextureID /*{0}*/;
        } PbrData /*= {}*/;
        uint32_t NormalTextureID /*{0}*/;
        float NormalScale /*{1.f}*/;
        uint32_t OcclusionTextureID /*{0}*/;
        float OcclusionStrength /*{1.f}*/;
        uint32_t EmissiveTextureID /*{0}*/;
        float3 EmissiveFactor /*{0.f}*/;
        float AlphaCutoff /*{0.5f}*/;  // The alpha value that determines the upper limit for fragments that should be discarded for
                                       // transparency.
    };

    static const uint32_t s_BINDLESS_IMAGE_BINDING   = 0;
    static const uint32_t s_BINDLESS_TEXTURE_BINDING = 1;
    static const uint32_t s_BINDLESS_SAMPLER_BINDING = 2;

    static const uint32_t s_MAX_BINDLESS_IMAGES   = 1 << 16;
    static const uint32_t s_MAX_BINDLESS_TEXTURES = 1 << 16;
    static const uint32_t s_MAX_BINDLESS_SAMPLERS = 1 << 11;

#ifndef __cplusplus

    [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<float4> RWImage2D_Heap[s_MAX_BINDLESS_IMAGES];
    [vk::binding(s_BINDLESS_TEXTURE_BINDING, 0)] Sampler2D Texture_Heap[s_MAX_BINDLESS_TEXTURES];
    [vk::binding(s_BINDLESS_SAMPLER_BINDING, 0)] SamplerState Sampler_Heap[s_MAX_BINDLESS_SAMPLERS];

    float3 RotateByQuat(const float3 v, const float4 quat /* x yzw = w xyz */)
    {
        const float3 t = 2 * cross(quat.yzw, v);
        return v + quat.x * t + cross(quat.yzw, t);
    }

#endif

}  // namespace Shaders
