#pragma once

#ifdef __cplusplus

namespace Radiant
{
    using float4x4 = glm::mat4;
    using float2   = glm::float2;
    using float3   = glm::float3;
    using float4   = glm::float4;

    using u16vec2 = glm::u16vec2;
    using u16vec4 = glm::u16vec4;

    using uint2 = glm::uvec2;
    using uint3 = glm::uvec3;

#endif

#define MAX_POINT_LIGHT_COUNT 1024
    // TODO: Implement spot lights
#define MAX_SPOT_LIGHT_COUNT 256

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

    // TODO: Instancing + culling(instance frustum occlusion meshlet triangle cone culling)
    struct ObjectInstanceData
    {
        float3 scale;
        float3 translation;
        // x - real part, yzw - imaginary part.
        // TODO: on c++ side convert from range[-1. 1] to [0,1] (*0.5 + 0.5) and then halfPackUnorm
        // unpacking *2-1
#ifdef __cplusplus
        u16vec4 orientation;
#else
    half4 orientation;
#endif
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

    // TODO: Use alpha for bCastShadows
    struct DirectionalLight
    {
        float3 Direction;
        uint32_t Color;  // NOTE: Packed color, alpha is not touched
        float Intensity;
        bool bCastShadows;
    };

    struct PointLight
    {
        Sphere sphere;
        uint32_t Color;  // NOTE: Packed color, alpha is not touched
        float Intensity;
    };

    namespace Shaders
    {
        static const float3 s_BASE_REFLECTIVITY = float3(0.04f);
        static const float s_KINDA_SMALL_NUMBER = 10.E-4f;
        static const float s_PI                 = 3.14159265f;
        static const float s_RcpPI              = 0.31830989f;

        // clang-format off
        // Indices for the cube (total 36 indices)
        static constexpr uint8_t g_CubeIndices[36] = {
            0,  1,  2,  0,  2,  3,   // Back face
            4,  5,  6,  4,  6,  7,   // Front face
            8,  9,  10, 8,  10, 11,  // Left face
            12, 13, 14, 12, 14, 15,  // Right face
            16, 17, 18, 16, 18, 19,  // Top face
            20, 21, 22, 20, 22, 23   // Bottom face
        };

        static constexpr float3 g_UnitCubeVertices[24] = {
            float3(-1.0f, -1.0f, -1.0f),  // Back face
            float3(1.0f, -1.0f, -1.0f),  
            float3(1.0f, 1.0f, -1.0f), 
            float3(-1.0f, 1.0f, -1.0f),

            float3(-1.0f, -1.0f, 1.0f),  // Front face
            float3(1.0f, -1.0f, 1.0f),   
            float3(1.0f, 1.0f, 1.0f),  
            float3(-1.0f, 1.0f, 1.0f),

            float3(-1.0f, -1.0f, -1.0f),  // Left face
            float3(-1.0f, -1.0f, 1.0f),  
            float3(-1.0f, 1.0f, 1.0f), 
            float3(-1.0f, 1.0f, -1.0f),

            float3(1.0f, -1.0f, -1.0f),  // Right face
            float3(1.0f, -1.0f, 1.0f),   
            float3(1.0f, 1.0f, 1.0f),  
            float3(1.0f, 1.0f, -1.0f),

            float3(-1.0f, 1.0f, -1.0f),  // Top face
            float3(1.0f, 1.0f, -1.0f),   
            float3(1.0f, 1.0f, 1.0f),  
            float3(-1.0f, 1.0f, 1.0f),

            float3(-1.0f, -1.0f, -1.0f),  // Bottom face
            float3(1.0f, -1.0f, -1.0f),  
            float3(1.0f, -1.0f, 1.0f), 
            float3(-1.0f, -1.0f, 1.0f),
        };
        // clang-format on

        static const uint32_t s_RAINBOW_COLOR_COUNT                 = 8;
        static const float4 s_RAINBOW_COLORS[s_RAINBOW_COLOR_COUNT] = {
            float4(1.0f, 0.0f, 0.0f, 1.0f),  // Red
            float4(0.0f, 1.0f, 0.0f, 1.0f),  // Green
            float4(0.0f, 0.0f, 1.0f, 1.0f),  // Blue
            float4(1.0f, 1.0f, 0.0f, 1.0f),  // Yellow
            float4(0.5f, 0.5f, 0.5f, 1.0f),  // Gray
            float4(0.0f, 1.0f, 1.0f, 1.0f),  // Turquoise
            float4(1.0f, 0.0f, 1.0f, 1.0f),  // Purple
            float4(1.0f, 1.0f, 1.0f, 1.0f)   // White
        };

        struct LightData
        {
            DirectionalLight Sun;
            uint32_t PointLightCount;
            PointLight PointLights[MAX_POINT_LIGHT_COUNT];
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
            float Zoom;
        };

        // NOTE: By default all textures in glTF are in sRGB color space, so we convert all textures firstly in linear RGB color space and
        // then apply in the end gamma correction.
        // NOTE: By default GfxContext creates white texture, so each texture ID is 0!
        struct GLTFMaterial
        {
            struct PBRData
            {
                uint32_t BaseColorFactor;
                uint16_t MetallicFactor;
                uint16_t RoughnessFactor;
                uint32_t AlbedoTextureID;
                uint32_t MetallicRoughnessTextureID;  // R = 0, G = roughness, B = metallic.
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

        [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<unorm float> RWImage2D_Heap_R8UNORM[s_MAX_BINDLESS_IMAGES];
        [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<float> RWImage2D_Heap_R32F[s_MAX_BINDLESS_IMAGES];
        [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<float2> RWImage2D_Heap_RG32F[s_MAX_BINDLESS_IMAGES];
        [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<float3> RWImage2D_Heap_RGB32F[s_MAX_BINDLESS_IMAGES];
        [vk::binding(s_BINDLESS_IMAGE_BINDING, 0)] RWTexture2D<float4> RWImage2D_Heap_RGBA32F[s_MAX_BINDLESS_IMAGES];
        [vk::binding(s_BINDLESS_TEXTURE_BINDING, 0)] Sampler2D Texture_Heap[s_MAX_BINDLESS_TEXTURES];
        [vk::binding(s_BINDLESS_TEXTURE_BINDING, 0)] SamplerCube Texture_Cube_Heap[s_MAX_BINDLESS_TEXTURES];
        [vk::binding(s_BINDLESS_SAMPLER_BINDING, 0)] SamplerState Sampler_Heap[s_MAX_BINDLESS_SAMPLERS];

        // converting from 2D array index to 1D array index
        inline uint flatten2D(const uint2 coords, const uint2 dim)
        {
            return coords.x + coords.y * dim.x;
        }

        // converting from 1D array index to 2D array index
        inline uint2 unflatten2D(const uint idx, const uint2 dim)
        {
            return uint2(idx % dim.x, idx / dim.x);
        }

        // converting from 3D array index to 1D array index
        inline uint flatten3D(const uint3 coords, const uint3 dim)
        {
            return flatten2D(coords.xy, dim.xy) + coords.z * dim.y * dim.x;
        }

        // converting from 1D array index to 3D array index
        inline uint3 unflatten3D(uint idx, const uint3 dim)
        {
            const uint z = idx / (dim.x * dim.y);
            idx -= (z * dim.x * dim.y);  // think of it, like we remove the height
            return uint3(unflatten2D(idx, dim.xy), z);
        }

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
        // XeGTAO uses same thing: https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.h#L169
        // also better explanation is right there:
        // https://www.youtube.com/watch?v=z1KG2Cwi1pk&list=PLU2nPsAdxKWQYxkmQ3TdbLsyc1l2j25XM&index=125&ab_channel=GameEngineSeries
        float ScreenSpaceDepthToView(const float fScreenDepth, const float4x4 projectionMatrix)
        {
            // NOTE: Projection matrix here I receive from camera data, it's in Col-Row major stored(c++ side + glm), 
            // but slang access is always Row-Col, so instead of this:
            // return -projectionMatrix[3][2] / (fScreenDepth + projectionMatrix[2][2]);
            // we do this:
            return -projectionMatrix[2][3] / (fScreenDepth + projectionMatrix[2][2]);
        }

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

        bool is_saturated(const float3 uvw)
        {
            return uvw.x >= 0.0f && uvw.x <= 1.0f && uvw.y >= 0.0f && uvw.y <= 1.0f && uvw.z >= 0.0f && uvw.z <= 1.0f;
        }

        float4 ScreenSpaceToView(const float2 uv, const float z, const float4x4 invProjectionMatrix)
        {
            const float4 p = mul(invProjectionMatrix, float4(uv * 2.0f - 1.0f, z, 1.0f));
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
            const bool3 cutoff  = linearRGB.rgb < float3(0.0031308f);
            const float3 higher = float3(1.055) * pow(linearRGB.rgb, float3(0.4166666f)) - float3(0.055);
            const float3 lower  = linearRGB.rgb * float3(12.92);

            return float4(lerp(higher, lower, (float3)cutoff), linearRGB.a);
        }

        // Converts a color from sRGB gamma to linear light gamma
        float4 sRGB2Linear(const float4 sRGB)
        {
            const bool3 cutoff  = sRGB.rgb < float3(0.04045);
            const float3 higher = pow((sRGB.rgb + float3(0.055)) * float3(0.9478673f), float3(2.4));
            const float3 lower  = sRGB.rgb * float3(0.0773994f);

            return float4(lerp(higher, lower, (float3)cutoff), sRGB.a);
        }

        // Microfacet distribution aligned to halfway vector
        float EvaluateDistributionGGX(const float NdotH, const float roughness)
        {
            const float a     = roughness * roughness;
            const float a2    = a * a;
            const float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;

            return a2 / (denom * denom) * Shaders::s_RcpPI;
        }

        float GeometrySchlickGGX(float NdotV, const float roughness)
        {
            const float k = (roughness + 1.0f) * (roughness + 1.0f) * 0.125f;
            return NdotV / (NdotV * (1.0f - k) + k);
        }

        // geom obstruction && geom shadowing. (Microfacets shadowing)
        float EvaluateGeometrySmith(const float NdotV, const float NdotL, const float roughness)
        {
            return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
        }

        // IBL geom obstruction && geom shadowing. (Microfacets shadowing)
        float EvaluateGeometrySmith_IBL(const float NdotV, const float NdotL, const float roughness)
        {
            const float k  = roughness * roughness * 0.5f;
            const float GL = NdotL / (NdotL * (1.0 - k) + k);
            const float GV = NdotV / (NdotV * (1.0 - k) + k);
            return GL * GV;
        }

        // (Reflectance depending on angle of incidence)
        // FresnelSchlick, evaluating ratio of base reflectivity looking perpendicularly towards surface.
        float3 EvaluateFresnelSchlick(const float NdotV, const float3 F0, const float3 F90 = float3(1.0f))
        {
            return F0 + (F90 - F0) * pow(1.0f - NdotV, 5.0f);
        }

        // (Reflectance depending on angle of incidence)
        // FresnelSchlick IBL version, evaluating ratio of base reflectivity looking perpendicularly towards surface.
        float3 EvaluateFresnelSchlickRoughness(const float NdotV, const float3 F0, const float roughness, const float3 F90 = float3(1.0f))
        {
            // The greater the roughness, the lesser the fresnel.
            return EvaluateFresnelSchlick(NdotV, F0, max(float3(1.0f - roughness), F0));
        }

        // http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
        // Inverses the number around decimal point. Returned number is in range [0, 1).
        float RadicalInverse_VdC(uint bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return float(bits) * 2.3283064365386963e-10;  // / 0x100000000
        }

        // Returns low-discrepancy 2d point in range [0, 1).
        float2 Hammersley2D(const uint i, const float invN)
        {
            return float2(float(i) * invN, RadicalInverse_VdC(i));
        }

        // Based on http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_slides.pdf
        float3 ImportanceSampleGGX(const float2 Xi, const float3 N, const float roughness)
        {
            // Maps a 2D point to a hemisphere with spread based on roughness
            const float a = roughness * roughness;  // Better for human perception as Epic Games does from Disney's research.

            const float phi      = 2.0 * Shaders::s_PI * Xi.x;
            const float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
            const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

            // From spherical coordinates to cartesian coordinates.
            const float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

            // Tangent space
            const float3 up        = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
            const float3 tangent   = normalize(cross(up, N));
            const float3 bitangent = cross(N, tangent);

            // Convert to world space
            return normalize(tangent * H.x + bitangent * H.y + N * H.z);
        }

        float3 TonemapACES(const float3 x)
        {
            const float a = 2.51f;
            const float b = 0.03f;
            const float c = 2.43f;
            const float d = 0.59f;
            const float e = 0.14f;
            return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
        }

        float3 TonemapReinhard(const float3 x)
        {
            return x / (x + float3(1.0f));
        }

        float3 ToneMapUncharted2(float3 x)
        {
            const float A        = 0.15f;
            const float B        = 0.50f;
            const float C        = 0.10f;
            const float D        = 0.20f;
            const float E        = 0.02f;
            const float F        = 0.30f;
            const float W        = 11.2f;
            const float exposure = 2.0f;

            x *= exposure;
            x                 = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
            const float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
            return x / white;
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

    }  // namespace Shaders

#ifdef __cplusplus

}  // namespace Radiant

#endif
