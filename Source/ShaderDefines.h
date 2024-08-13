#pragma once

#ifdef __cplusplus

namespace Radiant
{
    using float4x4 = glm::mat4;
    using float2   = glm::float2;
    using float3   = glm::float3;
    using float4   = glm::float4;

    using u16vec2 = glm::u16vec2;

    using uint2 = glm::uvec2;
    using uint3 = glm::uvec3;

#endif

// NOTE: AMD iGPU doesn't support storagePushConstant8/16, but I wanna preserve them on my NV dGPU so I can fastly transfer quantized data.
#define RENDER_FORCE_IGPU 0

    struct VertexPosition
    {
        float3 Position;
    };

    // https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-floattor-encoding/
    // https://www.jeremyong.com/graphics/2023/01/09/tangent-spaces-and-diamond-encoding/
    struct VertexAttribute
    {
        uint32_t Color;
#ifdef __cplusplus
        u16vec2 Normal;
        u16vec2 Tangent;
        u16vec2 UV;
#else
    half2 Normal;
    half2 Tangent;
    half2 UV;
#endif
        int16_t TSign;  // NOTE: Maybe put in the last tangent's bit?
    };

    struct Sphere
    {
        float3 Origin;
        float Radius;
    };

    struct AABB
    {
        float3 Min;
        float3 Max;
    };

    struct Plane
    {
        float3 Normal;
        float Distance;
    };

    struct DirectionalLight
    {
        float3 Direction;
        uint32_t Color;  // NOTE: Packed color, alpha is not touched
        float Intensity;
        // bool bCastShadows;
    };

    struct PointLight
    {
        Sphere sphere;
        uint32_t Color;  // NOTE: Packed color, alpha is not touched
        float Intensity;
        // bool bCastShadows;
    };

    namespace Shaders
    {
        static const float s_KINDA_SMALL_NUMBER = 10.E-4f;
        static const float s_PI                 = 3.14159265f;

        static const uint32_t s_RAINBOW_COLOR_COUNT                 = 8;
        static const float4 s_RAINBOW_COLORS[s_RAINBOW_COLOR_COUNT] = {
            float4(0.5f, 0.5f, 0.5f, 1.0f),  // Gray
            float4(0.0f, 0.0f, 1.0f, 1.0f),  // Blue
            float4(0.0f, 1.0f, 0.0f, 1.0f),  // Green
            float4(0.0f, 1.0f, 1.0f, 1.0f),  // Turquoise
            float4(1.0f, 0.0f, 0.0f, 1.0f),  // Red
            float4(1.0f, 0.0f, 1.0f, 1.0f),  // Purple
            float4(1.0f, 1.0f, 0.0f, 1.0f),  // Yellow
            float4(1.0f, 1.0f, 1.0f, 1.0f)   // White
        };

#define MAX_POINT_LIGHT_COUNT 100
        struct LightData
        {
            DirectionalLight Sun;
            uint32_t PointLightCount;
            PointLight PointLights[MAX_POINT_LIGHT_COUNT];
        };

        struct LightClusterList
        {
            uint32_t PointLightCount;
            uint32_t PointLightIndices[MAX_POINT_LIGHT_COUNT];
        };

        struct CameraData
        {
            float4x4 ProjectionMatrix;
            float4x4 ViewMatrix;
            float4x4 ViewProjectionMatrix;
            float4x4 InvProjectionMatrix;
            float4x4 InvViewProjectionMatrix;
            float2 FullResolution;
            float2 InvFullResolution;
            float3 Position;
            float2 zNearFar;
            float2 DepthUnpackConsts;  // x - depthLinearizeMul, y - depthLinearizeAdd
        };

        // NOTE: By default all textures in glTF are in sRGB color space, so we convert all textures firstly in linear RGB color space and
        // then apply in the end gamma correction. NOTE: By default GfxContext creates white texture, so each texture ID has 0 index!
        struct GLTFMaterial
        {
            struct PBRData
            {
                uint32_t BaseColorFactor;
                uint16_t MetallicFactor;
                uint16_t RoughnessFactor;
                uint32_t AlbedoTextureID;
                uint32_t MetallicRoughnessTextureID;
            } PbrData;
            uint32_t NormalTextureID;
            float NormalScale;
            uint32_t OcclusionTextureID;
            uint16_t OcclusionStrength;
            uint32_t EmissiveTextureID;
            float3 EmissiveFactor;
            float AlphaCutoff;  // The alpha value that determines the upper limit for fragments that should be discarded for
                                // transparency.
        };

        static const uint32_t s_BINDLESS_IMAGE_BINDING   = 0;
        static const uint32_t s_BINDLESS_TEXTURE_BINDING = 1;
        static const uint32_t s_BINDLESS_SAMPLER_BINDING = 2;

        static const uint32_t s_MAX_BINDLESS_IMAGES   = 1 << 16;
        static const uint32_t s_MAX_BINDLESS_TEXTURES = 1 << 16;
        static const uint32_t s_MAX_BINDLESS_SAMPLERS = 1 << 10;

#ifndef __cplusplus

        [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<float> RWImage2D_Heap_F1[s_MAX_BINDLESS_IMAGES];
        [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<float4> RWImage2D_Heap_F4[s_MAX_BINDLESS_IMAGES];
        [vk::binding(s_BINDLESS_TEXTURE_BINDING, 0)] Sampler2D Texture_Heap[s_MAX_BINDLESS_TEXTURES];
        [vk::binding(s_BINDLESS_SAMPLER_BINDING, 0)] SamplerState Sampler_Heap[s_MAX_BINDLESS_SAMPLERS];

        float3 RotateByQuat(const float3 v, const float4 quat /* x yzw = w xyz */)
        {
            const float3 t = 2 * cross(quat.yzw, v);
            return v + quat.x * t + cross(quat.yzw, t);
        }

        float3x3 QuatToRotMat3(const float4 q)
        {
            const float3 q2  = q.yzw * q.yzw;
            const float3 qq2 = q.yzw * q2;
            const float3 qq1 = q.yzw * q.x;

            return float3x3(1.0 - 2.0 * (q2.y + q2.z), 2.0 * (qq2.x + qq1.z), 2.0 * (qq2.y - qq1.y), 2.0 * (qq2.x - qq1.z),
                            1.0 - 2.0 * (q2.x + q2.z), 2.0 * (qq2.z + qq1.x), 2.0 * (qq2.y + qq1.y), 2.0 * (qq2.z - qq1.x),
                            1.0 - 2.0 * (q2.x + q2.y));
        }

        // clang-format off
        // NOTE: Formula is easily derived from projection perspective matrix multiplication && perspective division = Zout(depth in framebuffer).
        // depthLinearizeMul: (zFar * zNear) / (zFar - zNear); depthLinearizeAdd: zFar / (zFar - zNear); 
        // Also since we want ViewSpace(we are facing towards NEGATIVE Z) depth we NEGATE it.
        float ScreenSpaceDepthToView(const float fScreenSpaceDepth, const float2 depthUnpackConsts)
        {
            return -depthUnpackConsts.x / (depthUnpackConsts.y - fScreenSpaceDepth);
        }
        // clang-format on

        bool is_saturated(const float2 uv)
        {
            return uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f;
        }

        float4 ScreenSpaceToView(const float2 uv, const float z, const float4x4 invProjectionMatrix)
        {
            float4 p = mul(invProjectionMatrix, float4(uv * 2.0f - 1.0f, z, 1.0f));
            return p / p.w;
        }

        // ABGR unpack
        float4 UnpackUnorm4x8(const uint32_t packed)
        {
            return float4((packed >> 0) & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF, (packed >> 24) & 0xFF) *
                   0.0039215686274509803921568627451f;
        }

        float UnpackUnorm2x8(const uint16_t packed)
        {
            return (float)packed * 0.00001525902f;
        }

        // Input encoded float2 in range [0, 1] on each component.
        float3 DecodeOct(float2 f)
        {
            f = f * 2.0f - 1.0f;

            // https://twitter.com/Stubbesaurus/status/937994790553227264
            float3 n      = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
            const float t = max(-n.z, 0.0f);
            n.x += n.x >= 0.0f ? -t : t;
            n.y += n.y >= 0.0f ? -t : t;
            return normalize(n);
        }

        float2 OctWrap(float2 v)
        {
            const float2 t = 1.0f - abs(v.yx);
            return float2(v.x < 0.0f ? -t.x : t.x, v.y < 0.0f ? -t.y : t.y);
        }

        // Output encoded float2 in range [0, 1] on each component.
        float2 EncodeOct(float3 n)
        {
            n /= (abs(n.x) + abs(n.y) + abs(n.z));
            const float2 p = n.z > 0.0f ? n.xy : OctWrap(n.xy);
            return p * 0.5f + 0.5f;
        }

        // https://gamedev.stackexchange.com/questions/92015/optimized-linear-to-srgb-glsl
        // Converts a color from linear light gamma to sRGB gamma
        float4 Linear2sRGB(const float4 linearRGB)
        {
            const bool3 cutoff  = linearRGB.rgb < float3(0.0031308);
            const float3 higher = float3(1.055) * pow(linearRGB.rgb, float3(1.0 / 2.4)) - float3(0.055);
            const float3 lower  = linearRGB.rgb * float3(12.92);

            return float4(lerp(higher, lower, (float3)cutoff), linearRGB.a);
        }

        // Converts a color from sRGB gamma to linear light gamma
        float4 sRGB2Linear(const float4 sRGB)
        {
            const bool3 cutoff  = sRGB.rgb < float3(0.04045);
            const float3 higher = pow((sRGB.rgb + float3(0.055)) / float3(1.055), float3(2.4));
            const float3 lower  = sRGB.rgb / float3(12.92);

            return float4(lerp(higher, lower, (float3)cutoff), sRGB.a);
        }

#else

    // ABGR pack
    static uint32_t PackUnorm4x8(const float4 value) noexcept
    {
        const float4 fPacked = round(saturate(value) * 255.0f);
        return ((uint8_t)fPacked.w << 24) | ((uint8_t)fPacked.z << 16) | ((uint8_t)fPacked.y << 8) | (uint8_t)fPacked.x;
    }

    // ABGR unpack
    static float4 UnpackUnorm4x8(const uint32_t packed) noexcept
    {
        return float4((packed >> 0) & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF, (packed >> 24) & 0xFF) *
               0.0039215686274509803921568627451f;
    }

    static uint16_t PackUnorm2x8(const float value) noexcept
    {
        return static_cast<uint16_t>(value * 65535.0f);
    }

    static float2 OctWrap(float2 v) noexcept
    {
        const float2 t = float2(1.0f) - abs(float2(v.y, v.x));
        return float2(v.x < 0.0f ? -t.x : t.x, v.y < 0.0f ? -t.y : t.y);
    }

    // Output encoded float2 in range [0, 1].
    static float2 EncodeOct(float3 n) noexcept
    {
        n /= (abs(n.x) + abs(n.y) + abs(n.z));
        const float2 p = n.z > 0.0f ? n : OctWrap(n);
        return p * 0.5f + 0.5f;
    }

#endif

        // NOTE: Since my AMD iGPU has workgroup local size limits(1024), now each thread computes 3 clusters.
#define LIGHT_CLUSTER_SUBDIVISON_SIZE_X 16u
#define LIGHT_CLUSTER_SUBDIVISON_SIZE_Y 8u
#define LIGHT_CLUSTER_SUBDIVISON_SIZE_Z 8u

        // Clustered Shading, 3456 clusters(16x9x24), depth slices logarithmic, DOOM2016 has 16x8x24
        static constexpr uint3 s_LIGHT_CLUSTER_SUBDIVISIONS = uint3(16, 9, 24);

    }  // namespace Shaders

#ifdef __cplusplus

}  // namespace Radiant

#endif
