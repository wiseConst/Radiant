// setup_csm.slang

#include "../../../Source/ShaderDefines.hpp"
#include <csm_defines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const Shaders::DepthBounds *DepthBounds;
    Shaders::CascadedShadowMapsData *CSMData;
    float3 SunDirection; // NOTE: defines "sun position".
    float CascadeSplitLambda;
};
[[vk::push_constant]] PushConstantBlock u_PC;

// NOTE: For convenience shadows are reversed-z.

float glm_radians(const float degrees)
{
    return degrees * 0.01745329251994329576923690768489;
}

float4x4 glm_inverse(float4x4 m)
{
    // Slang row-column access:
    m = transpose(m);
     
    const float Coef00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
    const float Coef02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
    const float Coef03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];

    const float Coef04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
    const float Coef06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
    const float Coef07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];

    const float Coef08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
    const float Coef10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
    const float Coef11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];

    const float Coef12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
    const float Coef14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
    const float Coef15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];

    const float Coef16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
    const float Coef18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
    const float Coef19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];

    const float Coef20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
    const float Coef22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
    const float Coef23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

    const float4 Fac0 = float4(Coef00, Coef00, Coef02, Coef03);
    const float4 Fac1 = float4(Coef04, Coef04, Coef06, Coef07);
    const float4 Fac2 = float4(Coef08, Coef08, Coef10, Coef11);
    const float4 Fac3 = float4(Coef12, Coef12, Coef14, Coef15);
    const float4 Fac4 = float4(Coef16, Coef16, Coef18, Coef19);
    const float4 Fac5 = float4(Coef20, Coef20, Coef22, Coef23);

    const float4 Vec0 = float4(m[1][0], m[0][0], m[0][0], m[0][0]);
    const float4 Vec1 = float4(m[1][1], m[0][1], m[0][1], m[0][1]);
    const float4 Vec2 = float4(m[1][2], m[0][2], m[0][2], m[0][2]);
    const float4 Vec3 = float4(m[1][3], m[0][3], m[0][3], m[0][3]);

    const float4 Inv0 = float4(Vec1 * Fac0 - Vec2 * Fac1 + Vec3 * Fac2);
    const float4 Inv1 = float4(Vec0 * Fac0 - Vec2 * Fac3 + Vec3 * Fac4);
    const float4 Inv2 = float4(Vec0 * Fac1 - Vec1 * Fac3 + Vec3 * Fac5);
    const float4 Inv3 = float4(Vec0 * Fac2 - Vec1 * Fac4 + Vec2 * Fac5);

    const float4 SignA = float4(+1, -1, +1, -1);
    const float4 SignB = float4(-1, +1, -1, +1);
    float4x4 Inverse = float4x4(Inv0 * SignA, Inv1 * SignB, Inv2 * SignA, Inv3 * SignB);
    Inverse = transpose(Inverse);

    const float4 Row0 = float4(Inverse[0][0], Inverse[1][0], Inverse[2][0], Inverse[3][0]);

    const float4 Dot0 = float4(m[0] * Row0);
    const float Dot1 = (Dot0.x + Dot0.y) + (Dot0.z + Dot0.w);

    const float OneOverDeterminant = 1.0f / Dot1;
    return OneOverDeterminant * Inverse;
}

float4x4 glm_perspectiveRH_ZO(const float fovY, const float aspectRatio, const float zNear, const float zFar)
{
    const float tanHalfFovY = tan(fovY * 0.5f);

    float4x4 Result = float4x4(0);
    Result[0][0] = 1.0f / (aspectRatio * tanHalfFovY);
    Result[1][1] = 1.0f / (tanHalfFovY);
    Result[2][2] = zFar / (zNear - zFar);
    Result[2][3] = -1.0f;
    Result[3][2] = -(zFar * zNear) / (zFar - zNear);
    // Slang row-column access:
    // return Result;
     return transpose(Result);
}

float4x4 glm_lookAtRH(const float3 eye, const float3 center, const float3 up)
{
     const float3 f = normalize(center - eye);
     const float3 s = normalize(cross(f, up));
     const float3 u = cross(s, f);

     float4x4 ret = 0;

     ret[0][0] = s.x;
     ret[0][1] = u.x;
     ret[0][2] = -f.x;
     ret[0][3] = 0.0;

     ret[1][0] = s.y;
     ret[1][1] = u.y;
     ret[1][2] = -f.y;
     ret[1][3] = 0.0;

     ret[2][0] = s.z;
     ret[2][1] = u.z;
     ret[2][2] = -f.z;
     ret[2][3] = 0.0;

     ret[3][0] = -dot(s, eye);
     ret[3][1] = -dot(u, eye);
     ret[3][2] = dot(f, eye);
     ret[3][3] = 1.0;

     // Slang row-column access:
     // return ret;
     return transpose(ret);
}

float4x4 glm_orthoRH_ZO(const float left, const float right, const float bottom, const float top, const float zNear, const float zFar)
{
    float4x4 ret = 0;

    ret[0][0] = 2.0f / (right - left);
    ret[1][1] = 2.0f / (top - bottom);
    ret[2][2] = -1.0f / (zFar - zNear);
    ret[3][0] = -(right + left) / (right - left);
    ret[3][1] = -(top + bottom) / (top - bottom);
    ret[3][2] = -zNear / (zFar - zNear);
    ret[3][3] = 1.0;

    // Slang row-column access:
    // return ret;
    return transpose(ret);
}

/*
      Calculate frustum split depths and matrices for the shadow map cascades
      Based on https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
*/
void SetupCSMData(const float cameraFovY, const float cameraAR, const float zNear,
                                                                   const float zFar, const float4x4 cameraView, const float3 L)
{
    u_PC.CSMData.MinMaxCascadeDistance = float2(zNear, zFar);

    // Calculate split depths based on view camera frustum
    // Based on method presented in
    // https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
    const float range = zFar - zNear;
    const float ratio = zFar / zNear;
    for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        const float p = (i + 1) / (float)SHADOW_MAP_CASCADE_COUNT;
        const float logPart = zNear * pow(ratio, p);
        const float uniformPart = zNear + range * p;
        const float d = uniformPart + u_PC.CascadeSplitLambda * (logPart - uniformPart);
        u_PC.CSMData.CascadeSplits[i] = (d - zNear) / range;
    }

    float4x4 shadowCameraProj = glm_perspectiveRH_ZO(cameraFovY, cameraAR, zNear, zFar);
    shadowCameraProj[1][1] *= -1.0f;

    const float4x4 shadowViewProj = mul(shadowCameraProj, cameraView);
    const float4x4 NDCToWorldSpaceMatrix = glm_inverse(shadowViewProj);

    float lastSplitDist = 0.0f;
    for (uint i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        const float splitDist = u_PC.CSMData.CascadeSplits[i];

        // Starting with vulkan NDC coords, ending with frustum world space.
        float3 frustumCornersWS[8] = {
            float3(-1.0f, 1.0f, 0.0f), float3(1.0f, 1.0f, 0.0f),   //
            float3(1.0f, -1.0f, 0.0f), float3(-1.0f, -1.0f, 0.0f), //
            float3(-1.0f, 1.0f, 1.0f), float3(1.0f, 1.0f, 1.0f),   //
            float3(1.0f, -1.0f, 1.0f), float3(-1.0f, -1.0f, 1.0f)  //
        };

        // Project frustum corners into world space.
        for (uint j = 0; j < 8; ++j)
        {
            const float4 cornerWS = mul(NDCToWorldSpaceMatrix, float4(frustumCornersWS[j], 1.0f));
            frustumCornersWS[j] = cornerWS.xyz / cornerWS.w;
        }

        // Adjust frustum to current subfrustum.
        for (uint j = 0; j < 4; ++j)
        {
            const float3 cornerRay = frustumCornersWS[j + 4] - frustumCornersWS[j];
            frustumCornersWS[j + 4] = frustumCornersWS[j] + cornerRay * splitDist;
            frustumCornersWS[j] += cornerRay * lastSplitDist;
        }

        // Get frustum center.
        float3 frustumCenterWS = float3(0.0f);
        for (uint j = 0; j < 8; ++j)
            frustumCenterWS += frustumCornersWS[j];

        frustumCenterWS /= 8.0f;

        // Find the longest radius of the frustum.
        static constexpr float MaxFloat = 3.402823466e+38F;
        float radius = -MaxFloat;
        for (uint j = 0; j < 8; ++j)
        {
            const float diff = length(frustumCornersWS[j] - frustumCenterWS);
            radius = max(radius, diff);
        }
        radius = ceil(radius * 16.0f) / 16.0f;

        const float3 maxExtents = float3(radius);
        const float3 minExtents = -maxExtents;

        const float4x4 lightView = glm_lookAtRH(frustumCenterWS + L + float3(Shaders::s_KINDA_SMALL_NUMBER, 0.0f, 0.0f), frustumCenterWS,
                                               float3(0.0f, 1.0f, 0.0f));
        float4x4 lightOrthoProj = glm_orthoRH_ZO(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z);
        lightOrthoProj[1][1] *= -1.0f;

        // https://www.gamedev.net/forums/topic/591684-xna-40---shimmering-shadow-maps/
        if (/*s_bCascadeTexelSizedIncrements*/ true)
        {
            // Shimmering fix: move in texel-sized increments.
            // (finding out how much we need to move the orthographic matrix so it matches up with shadow map)
            const float4x4 shadowMatrix = mul(lightOrthoProj, lightView);
            float4 shadowOrigin = mul(shadowMatrix, float4(0.0f, 0.0f, 0.0f, 1.0f));
            shadowOrigin *= (float)SHADOW_MAP_CASCADE_SIZE * 0.5f;

            const float4 roundedOrigin = round(shadowOrigin);
            float4 roundOffset = roundedOrigin - shadowOrigin;
            roundOffset = roundOffset * 2.0f / (float)SHADOW_MAP_CASCADE_SIZE;
            roundOffset.z = roundOffset.w = 0.0f;

           // Slang row-column access:
           // lightOrthoProj[3] += roundOffset;
            lightOrthoProj[0][3] += roundOffset.x;
            lightOrthoProj[1][3] += roundOffset.y;
        }

        lastSplitDist = splitDist;
        u_PC.CSMData.ViewProjectionMatrix[i] = mul(lightOrthoProj, lightView);
        u_PC.CSMData.CascadeSplits[i] = zNear + splitDist * range;
    }
}

[numthreads(SHADOWS_SETUP_WG_SIZE_X, 1, 1)]
[shader("compute")]
void computeMain(const uint groupIndex : SV_GroupIndex)
{
    // Shader uses 1 thread, but potentially can use SHADOW_MAP_CASCADE_COUNT, so thread=cascade.
    if (!WaveIsFirstLane()) return;

    // reversed Z, so we swap MinMaxZ
    const float zNear = -Shaders::ScreenSpaceDepthToView(asfloat(u_PC.DepthBounds.MinMaxZ.y), u_PC.CameraData->ProjectionMatrix);
    const float zFar = -Shaders::ScreenSpaceDepthToView(asfloat(u_PC.DepthBounds.MinMaxZ.x), u_PC.CameraData->ProjectionMatrix);

    // printf("zNear: %f, zFar: %f\n", zNear, zFar);

    const float aspectRatio = u_PC.CameraData.FullResolution.x / u_PC.CameraData.FullResolution.y;
    SetupCSMData(glm_radians(u_PC.CameraData.Zoom), aspectRatio, zNear, zFar, u_PC.CameraData.ViewMatrix, normalize(u_PC.SunDirection));
}
