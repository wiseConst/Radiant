// screen_space_shadows.slang

#include "../../../Source/ShaderDefines.h"

static const uint32_t g_sss_max_steps        = 16;    // Max ray steps, affects quality and performance.
static const float    g_sss_max_ray_distance = 0.1f;  // Max shadow length, longer shadows are less accurate.
static const float    g_sss_thickness        = 0.05f; // Depth test thickness.
static const float    g_sss_max_delta_from_original_depth = 0.005f; // The maximum allowed depth deviation from the original pixel (a big deviation decreased the probabilty that the pixel is the occluder).
static const float    g_sss_step_length = g_sss_max_ray_distance / (float)g_sss_max_steps;

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    uint32_t DepthTextureID;
    uint32_t SSSTextureID;
    float3 SunDirection;
};
[vk::push_constant] PushConstantBlock u_PC;

float2 project_uv(const float3 pos, const float4x4 projectionMatrix)
{
    float4 sv_position = mul(projectionMatrix, float4(pos, 1.0f));
    return float2((sv_position.xy / sv_position.w) * 0.5f + 0.5f);
}

static const uint32_t g_sss_mode = 1; // 0 - hard, 1 - soft, 2 - very soft.

float ScreenSpaceShadows(const float2 uv, const float3 viewPos, const float3 lightDirection)
{
    // Compute ray position and direction (in view-space)
    float3 rayPos = viewPos;
    const float3 rayDir = normalize(mul(u_PC.CameraData->ViewMatrix, float4(lightDirection, 0.0f)).xyz);
    
    // Compute ray step
    const float3 rayStep = rayDir * g_sss_step_length;

    // Ray march towards the light
    float occluded = 0.0f;
    float softOccluded = 0.0f;
    float firstHit = g_sss_max_steps;
    float2 rayUV = 0.0f;
    for(uint32_t i = 0; i < g_sss_max_steps; ++i)
    {
        // Step the ray
        rayPos += rayStep;
        rayUV  = project_uv(rayPos, u_PC.CameraData->ProjectionMatrix);

        // Ensure the UV coordinates are inside the screen
        if (!Shaders::is_saturated(rayUV)) continue;

        // Compute the difference between the ray's and the camera's depth
        const float sampledDepthVS = Shaders::ScreenSpaceDepthToView(Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(rayUV, 0).r, u_PC.CameraData->DepthUnpackConsts);
        const float depthDelta = rayPos.z - sampledDepthVS;

        // Check if the camera can't "see" the ray (ray depth must be larger than the camera depth, so positive depth_delta)
        const bool bCanCameraSeeTheRay = (depthDelta > 0.0f) && (depthDelta < g_sss_thickness);
        const bool bIsOccludedByOriginalPixel = abs(rayPos.z - viewPos.z) < g_sss_max_delta_from_original_depth;
        if (bCanCameraSeeTheRay && bIsOccludedByOriginalPixel)
        {
            firstHit = min(firstHit, float(i));

            occluded += 1.0f;
            softOccluded += saturate(g_sss_thickness - depthDelta);
        }
    }

    float shadow = 1.0f;
    if (g_sss_mode == 2)
    {
        // very soft occlusion, includes distance falloff above
        shadow = softOccluded * (1.0f - (firstHit / (float)g_sss_max_steps));
        shadow = 1.0 - saturate(shadow);
        shadow *= shadow;
    }
    else if (g_sss_mode == 1)
    {
        // soft occlusion
        shadow = occluded * (1.0 - (firstHit / (float)g_sss_max_steps));
        shadow = 1.0 - saturate(shadow);
        shadow *= shadow;
    }
    else // == 0
    {
        // hard occlusion
        shadow = saturate(1.0f - occluded);
    }

    return shadow;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 gid : SV_DispatchThreadID)
{
    const float2 uv = float2(gid.xy) * u_PC.CameraData->InvFullResolution;

    const float3 viewPos = Shaders::ScreenSpaceToView(uv, Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(uv, 0).r, u_PC.CameraData->InvProjectionMatrix).xyz;
    const float3 lightDir = u_PC.SunDirection;                    // Vector from origin to sun
    Shaders::RWImage2D_Heap_R32F[u_PC.SSSTextureID][gid.xy] = 1.0f;//1.0f - ScreenSpaceShadows(uv, viewPos, lightDir);
}
