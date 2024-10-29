// setup_csm.slang

#include "../../../Source/ShaderDefines.hpp"
#include <csm_defines.hpp>

// The idea is each thread is a cascade, 
// so we dispatch only one workgroup at all.
// [numthreads(SHADOW_MAP_CASCADE_COUNT, 1, 1)]
// groupshared float cascadeSplits[4]

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const Shaders::DepthBounds *DepthBounds;
    Shaders::CascadedShadowMapsData *CSMData;
    float3 SunDirection; // NOTE: defines "sun position".
    float CascadeSplitLambda;
};
[[vk::push_constant]] PushConstantBlock u_PC;

float glm_radians(float degrees)
{
    return degrees * 0.01745329251994329576923690768489;
}

// Everything below is reversed, cuz of slang's Row-Col access.

float4x4 glm_perspective(const float fovy, const float aspect, const float zNear, const float zFar)
{
    const float tanHalfFovy = tan(fovy * 0.5f);

    float4x4 Result = float4x4(0.0f);
    Result[0][0] = 1.0f / (aspect * tanHalfFovy);
    Result[1][1] = 1.0f / (tanHalfFovy);
    Result[2][2] = zFar / (zNear - zFar);
    Result[3][2] = -1.0f;
    Result[2][3] = -(zFar * zNear) / (zFar - zNear);
    return Result;
}

float4x4 glm_inverse(const float4x4 m)
{
    const float Coef00 = m[2][2] * m[3][3] - m[2][3] * m[3][2];
    const float Coef02 = m[2][1] * m[3][3] - m[2][3] * m[3][1];
    const float Coef03 = m[2][1] * m[3][2] - m[2][2] * m[3][1];

    const float Coef04 = m[1][2] * m[3][3] - m[1][3] * m[3][2];
    const float Coef06 = m[1][1] * m[3][3] - m[1][3] * m[3][1];
    const float Coef07 = m[1][1] * m[3][2] - m[1][2] * m[3][1];

    const float Coef08 = m[1][2] * m[2][3] - m[1][3] * m[2][2];
    const float Coef10 = m[1][1] * m[2][3] - m[1][3] * m[2][1];
    const float Coef11 = m[1][1] * m[2][2] - m[1][2] * m[2][1];

    const float Coef12 = m[0][2] * m[3][3] - m[0][3] * m[3][2];
    const float Coef14 = m[0][1] * m[3][3] - m[0][3] * m[3][1];
    const float Coef15 = m[0][1] * m[3][2] - m[0][2] * m[3][1];

    const float Coef16 = m[0][2] * m[2][3] - m[0][3] * m[2][2];
    const float Coef18 = m[0][1] * m[2][3] - m[0][3] * m[2][1];
    const float Coef19 = m[0][1] * m[2][2] - m[0][2] * m[2][1];

    const float Coef20 = m[0][2] * m[1][3] - m[0][3] * m[1][2];
    const float Coef22 = m[0][1] * m[1][3] - m[0][3] * m[1][1];
    const float Coef23 = m[0][1] * m[1][2] - m[0][2] * m[1][1];

    const float4 Fac0 = float4(Coef00, Coef00, Coef02, Coef03);
    const float4 Fac1 = float4(Coef04, Coef04, Coef06, Coef07);
    const float4 Fac2 = float4(Coef08, Coef08, Coef10, Coef11);
    const float4 Fac3 = float4(Coef12, Coef12, Coef14, Coef15);
    const float4 Fac4 = float4(Coef16, Coef16, Coef18, Coef19);
    const float4 Fac5 = float4(Coef20, Coef20, Coef22, Coef23);

    const float4 Vec0 = float4(m[0][1], m[0][0], m[0][0], m[0][0]);
    const float4 Vec1 = float4(m[1][1], m[1][0], m[1][0], m[1][0]);
    const float4 Vec2 = float4(m[2][1], m[2][0], m[2][0], m[2][0]);
    const float4 Vec3 = float4(m[3][1], m[3][0], m[3][0], m[3][0]);

    const float4 Inv0 = float4(Vec1 * Fac0 - Vec2 * Fac1 + Vec3 * Fac2);
    const float4 Inv1 = float4(Vec0 * Fac0 - Vec2 * Fac3 + Vec3 * Fac4);
    const float4 Inv2 = float4(Vec0 * Fac1 - Vec1 * Fac3 + Vec3 * Fac5);
    const float4 Inv3 = float4(Vec0 * Fac2 - Vec1 * Fac4 + Vec2 * Fac5);

    const float4 SignA = float4(+1, -1, +1, -1);
    const float4 SignB = float4(-1, +1, -1, +1);
    const float4x4 Inverse = transpose(float4x4(Inv0 * SignA, Inv1 * SignB, Inv2 * SignA, Inv3 * SignB));
    // transpose and change indices?
    const float4 Row0 = float4(Inverse[0][0], Inverse[0][1], Inverse[0][2], Inverse[0][3]);

    const float4 Dot0 = float4(m[0] * Row0);
    const float Dot1 = (Dot0.x + Dot0.y) + (Dot0.z + Dot0.w);

    // rcp?
    const float OneOverDeterminant = 1.0f / Dot1;

    return Inverse * OneOverDeterminant;
}

float4x4 glm_lookAt(const float3 eye, const float3 center, const float3 up)
{
     const float3 f = float3(normalize(center - eye));
     const float3 s = float3(normalize(cross(f, up)));
     const float3 u = float3(cross(s, f));

    float4x4 Result = float4x4(1);
    Result[0][0] = s.x;
    Result[1][0] = s.y;
    Result[0][2] = s.z;
    Result[1][0] = u.x;
    Result[1][1] = u.y;
    Result[1][2] = u.z;
    Result[2][0] = -f.x;
    Result[2][1] = -f.y;
    Result[2][2] = -f.z;
    Result[0][3] = -dot(s, eye);
    Result[1][3] = -dot(u, eye);
    Result[2][3] = dot(f, eye);
    return Result;
}

float4x4 glm_ortho(const float left, const float right, const float bottom, const float top, const float zNear, const float zFar)
{
    float4x4 Result = float4x4(1.0f);
    Result[0][0] = 2.0f / (right - left);
    Result[1][1] = 2.0f / (top - bottom);
    Result[2][2] = -1.0f / (zFar - zNear);
    Result[0][3] = -(right + left) / (right - left);
    Result[1][3] = -(top + bottom) / (top - bottom);
    Result[2][3] = -zNear / (zFar - zNear);
    return Result;
}

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain()
{
    // reveresed z!!
    const float zNear = asfloat(u_PC.DepthBounds.MinMaxZ.x);//u_PC.CameraData.zNearFar.y;
    const float zFar = asfloat(u_PC.DepthBounds.MinMaxZ.y);  // u_PC.CameraData.zNearFar.x;

    printf("zNear: %f, zFar: %f", zNear, zFar);

    const float aspectRatio = u_PC.CameraData.FullResolution.x / u_PC.CameraData.FullResolution.y;
    const float4x4 projMain = glm_perspective(glm_radians(u_PC.CameraData.Zoom), aspectRatio, zNear, zFar);
    const float4x4 viewProjMain = mul(projMain, u_PC.CameraData.ViewMatrix);
    const float4x4 invViewProj = glm_inverse(viewProjMain);


    const float range = zFar - zNear;
    const float ratio = zFar / zNear;

    // Calculate split depths based on view camera frustum
    // Based on method presented in
    // https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
    for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        const float p = (i + 1) / float(SHADOW_MAP_CASCADE_COUNT);
        const float logSplit = zNear * pow(ratio, p);
        const float uniformSplit = zNear + range * p;
        const float d = uniformSplit + u_PC.CascadeSplitLambda * (logSplit - uniformSplit);
        u_PC.CSMData.CascadeSplits[i] = (d - zNear) / range;
    }

    float lastCascadeSplit = 0.0f;
    for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        float splitDist = u_PC.CSMData.CascadeSplits[i];
        float3 frustumCorners[8] = {
            float3(-1.0f, 1.0f, 0.0f),  float3(1.0f, 1.0f, 0.0f), float3(1.0f, -1.0f, 0.0f), float3(-1.0f, -1.0f, 0.0f),
            float3(-1.0f, 1.0f, 1.0f),  float3(1.0f, 1.0f, 1.0f), float3(1.0f, -1.0f, 1.0f), float3(-1.0f, -1.0f, 1.0f),
        };

        // Project frustum corners into world space.
        for (uint j = 0; j < 8; ++j)
        {
            float4 invCorner = mul(invViewProj, float4(frustumCorners[j], 1.0f));
            frustumCorners[j] = invCorner.xyz / invCorner.w;
        }

        // Get the corners of the current cascade slice of the view frustum.
        for (uint j = 0; j < 4; ++j)
        {
            const float3 dist = frustumCorners[j + 4] - frustumCorners[j];
            frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
            frustumCorners[j] = frustumCorners[j] + (dist * lastCascadeSplit);
        }

        // Get frustum center
        float3 frustumCenter = float3(0.0f);
        for (uint j = 0; j < 8; ++j)
        {
            frustumCenter += frustumCorners[j];
        }
        frustumCenter /= 8.0f;

        const float4x4 lightViewMatrix =
                glm_lookAt(frustumCenter + u_PC.SunDirection + Shaders::s_KINDA_SMALL_NUMBER, frustumCenter, float3(0.0f, 1.0f, 0.0f));

        float3 minExtents = float3(0.0f);
        float3 maxExtents = float3(1.0f);
        //if (s_bStabilizeCascades)
        //{
        //    f32 radius = 0.0f;
        //    for (u32 j = 0; j < 8; ++j)
        //    {
        //        const f32 distance = glm::length(frustumCorners[j] - frustumCenter);
        //        radius = glm::max(radius, distance);
        //    }
        //    radius = std::ceil(radius * 16.0f) / 16.0f;
        //    /*             const f32 pixelSize = radius / SHADOW_MAP_CASCADE_SIZE;
        //                 radius              = glm::round(radius / pixelSize) * pixelSize;*/

        //    maxExtents = glm::vec3(radius);
        //    minExtents = -maxExtents;
        //}
        //else
        {
            static constexpr float MaxFloat = 3.402823466e+38F;
            float3 minPointLVS = float3(MaxFloat);
            float3 maxPointLVS = float3(-MaxFloat);
            for (uint j = 0; j < 8; ++j)
            {
                const float3 cornerLVS = mul(lightViewMatrix, float4(frustumCorners[j], 1.0f)).xyz;
                minPointLVS = min(minPointLVS, cornerLVS);
                maxPointLVS = max(maxPointLVS, cornerLVS);
            }

            // Adjust the min/max to accommodate the filtering size
     //       const float scale = (SHADOW_MAP_CASCADE_SIZE + 9.0f) / float(SHADOW_MAP_CASCADE_SIZE);

            minExtents = minPointLVS;
            maxExtents = maxPointLVS;

            //minExtents.x *= scale;
            //minExtents.y *= scale;

            //maxExtents.x *= scale;
            //maxExtents.y *= scale;
        }

        float4x4 lightOrthoMatrix =
                glm_ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z);
        lightOrthoMatrix[1][1] *= -1; // Vulkan flip viewport

        // Store split distance and matrix in cascade
        u_PC.CSMData.CascadeSplits[i] = (zNear + splitDist * range) * -1.0f;
        u_PC.CSMData.ViewProjectionMatrix[i] = mul(lightOrthoMatrix, lightViewMatrix);
        lastCascadeSplit = splitDist;
    }
}
