// ssao_cs.slang
// https://wickedengine.net/2019/09/improved-normal-reconstruction-from-depth

#include <../../../Source/ShaderDefines.hpp>
#include <ssao_defines.hpp>

#if USE_THREAD_GROUP_TILING_X
#include <../ThreadGroupTilingX.slang>
#endif

struct PushConstantBlock
{
    uint DstSSAOTextureID;
    uint DepthTextureID;
    const Shaders::CameraData *CameraData;
#if USE_THREAD_GROUP_TILING_X
    uint3 WorkGroupNum;
#endif
};
[[vk::push_constant]] PushConstantBlock u_PC;

static const uint TILE_BORDER = 1;
// Don't forget to take borders from both sides(TILE_BORDER * 2) into account.
static const uint TILE_SIZE_X = SSAO_WG_SIZE_X + TILE_BORDER * 2;
static const uint TILE_SIZE_Y = SSAO_WG_SIZE_Y + TILE_BORDER * 2;
static const uint2 TILE_SIZE = uint2(TILE_SIZE_X, TILE_SIZE_Y);

// We separate the Z values into a deinterleaved array, because those will be loaded more frequently
// than XY components, when we determine the best corner points.
groupshared float2 gs_Tile_XY_VS[TILE_SIZE_X * TILE_SIZE_Y]; // view space position XY
groupshared float gs_Tile_Z_VS[TILE_SIZE_X * TILE_SIZE_Y]; // view space position Z

[numthreads(SSAO_WG_SIZE_X, SSAO_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(uint3 DTid : SV_DispatchThreadID, const uint3 Gid : SV_GroupID, const uint3 GTid : SV_GroupThreadID, const uint GroupIndex : SV_GroupIndex)
{
#if USE_THREAD_GROUP_TILING_X
    const uint2 srcDTid = DTid.xy;
    DTid.xy = ThreadGroupTilingX(u_PC.WorkGroupNum.xy, Shaders::g_SSAO_WG_SIZE, max(SSAO_WG_SIZE_X, SSAO_WG_SIZE_Y), GTid.xy, Gid.xy);
#endif
    const int2 tileUpperLeft = Gid.xy * Shaders::g_SSAO_WG_SIZE - TILE_BORDER;
    [loop]
    for (uint t = GroupIndex; t < TILE_SIZE_X * TILE_SIZE_Y; t += SSAO_WG_SIZE_X * SSAO_WG_SIZE_Y) // each thread maximum loads 2 texels
    {
        const uint2 pixel = tileUpperLeft + Shaders::unflatten2D(t, TILE_SIZE);
        const float2 uv = (pixel + 0.5f) * u_PC.CameraData.InvFullResolution;
        const float depth = Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(uv, 0).r;
        const float3 positionVS = Shaders::ScreenSpaceToView(uv, depth, u_PC.CameraData.InvProjectionMatrix).xyz;
        gs_Tile_XY_VS[t] = positionVS.xy;
        gs_Tile_Z_VS[t] = positionVS.z;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 baseCoordsGT = GTid.xy + TILE_BORDER;
    const uint crossIdx[5] = {
        Shaders::flatten2D(baseCoordsGT, TILE_SIZE),               // 0: center
        Shaders::flatten2D(baseCoordsGT + int2(1, 0), TILE_SIZE),  // 1: right
        Shaders::flatten2D(baseCoordsGT + int2(-1, 0), TILE_SIZE), // 2: left
        Shaders::flatten2D(baseCoordsGT + int2(0, 1), TILE_SIZE),  // 3: down
        Shaders::flatten2D(baseCoordsGT + int2(0, -1), TILE_SIZE), // 4: up
    };

    const float centerZ = gs_Tile_Z_VS[crossIdx[0]];
    const uint bestHorizontalZ = abs(gs_Tile_Z_VS[crossIdx[1]] - centerZ) < abs(gs_Tile_Z_VS[crossIdx[2]] - centerZ) ? 1 : 2;
    const uint bestVerticalZ = abs(gs_Tile_Z_VS[crossIdx[3]] - centerZ) < abs(gs_Tile_Z_VS[crossIdx[4]] - centerZ) ? 3 : 4;

    // CCW order
    float3 P1 = 0.0f, P2 = 0.0f;
    if (bestHorizontalZ == 1 && bestVerticalZ == 4)
    {
        P1 = float3(gs_Tile_XY_VS[crossIdx[1]], gs_Tile_Z_VS[crossIdx[1]]);
        P2 = float3(gs_Tile_XY_VS[crossIdx[4]], gs_Tile_Z_VS[crossIdx[4]]);
    }
    else if (bestHorizontalZ == 1 && bestVerticalZ == 3)
    {
        P1 = float3(gs_Tile_XY_VS[crossIdx[3]], gs_Tile_Z_VS[crossIdx[3]]);
        P2 = float3(gs_Tile_XY_VS[crossIdx[1]], gs_Tile_Z_VS[crossIdx[1]]);
    }
    else if (bestHorizontalZ == 2 && bestVerticalZ == 4)
    {
        P1 = float3(gs_Tile_XY_VS[crossIdx[4]], gs_Tile_Z_VS[crossIdx[4]]);
        P2 = float3(gs_Tile_XY_VS[crossIdx[2]], gs_Tile_Z_VS[crossIdx[2]]);
    }
    else if (bestHorizontalZ == 2 && bestVerticalZ == 3)
    {
        P1 = float3(gs_Tile_XY_VS[crossIdx[2]], gs_Tile_Z_VS[crossIdx[2]]);
        P2 = float3(gs_Tile_XY_VS[crossIdx[3]], gs_Tile_Z_VS[crossIdx[3]]);
    }

    const float3 P0 = float3(gs_Tile_XY_VS[crossIdx[0]], gs_Tile_Z_VS[crossIdx[0]]);
    const float3 N = normalize(cross(P2 - P0, P1 - P0)); // normal points towards positive Z, since camera points towards neg Z in view space.

    const float2 baseUV = (DTid.xy + 0.5f) * u_PC.CameraData.InvFullResolution;
    const uint2 rotationUV = uint2(baseUV * u_PC.CameraData->FullResolution) % 4;
    const float3 noise = normalize(Shaders::GetRotationVector(rotationUV.y * 4 + rotationUV.x) * 2.0f - 1.0f);

    const float3 viewPos = P0;
    const float3 T = normalize(noise - N * dot(N, noise));
    const float3 B = cross(N, T);
    const float3x3 TBN = transpose(float3x3(T, B, N));

    float ao = 0.0f;
    [unroll]
    for (uint32_t i = 0; i < Shaders::g_SampleCount; ++i)
    {
        const float3 sample = viewPos + mul(TBN, Shaders::GetHemisphereSample8(i)) * Shaders::g_SampleRadius;

        float4 offsetUV = mul(u_PC.CameraData->ProjectionMatrix, float4(sample, 1.0f));
        offsetUV.xy = (offsetUV.xy / offsetUV.w) * 0.5f + 0.5f;

#define REUSE_DEPTH_GS 0
#if REUSE_DEPTH_GS
        // NOTE: 
        // 1) somewhy it's slow a bit
        // 2) gives patterns
        // 3) Even best if-path accross threads inside warp sucks :(
        float sampledDepth = 0.0f;
        const uint2 pixel = uint2(offsetUV.xy * u_PC.CameraData.FullResolution - float2(0.5f));
        const uint sampledDepthIdx = Shaders::flatten2D(pixel - tileUpperLeft, TILE_SIZE);
        if (WaveActiveAllTrue(sampledDepthIdx < TILE_SIZE_X * TILE_SIZE_Y))
        {
            sampledDepth = gs_Tile_Z_VS[sampledDepthIdx];
        }
        else
        {
            sampledDepth = Shaders::ScreenSpaceDepthToView( Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(offsetUV.xy, 0).r, u_PC.CameraData->ProjectionMatrix);
        }
#else
        const float sampledDepth = Shaders::ScreenSpaceDepthToView(Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(offsetUV.xy, 0).r, u_PC.CameraData->ProjectionMatrix);
#endif
        
        const float rangeCheck = smoothstep(0.0f, 1.0f, Shaders::g_SampleRadius / abs(viewPos.z - sampledDepth));
        ao += step(sample.z + Shaders::g_SampleBias, sampledDepth) * rangeCheck;
    }
    ao *= Shaders::g_SampleCountInv;
#if USE_THREAD_GROUP_TILING_X
    Shaders::RWImage2D_Heap_R8UNORM[u_PC.DstSSAOTextureID][srcDTid] = pow(ao, Shaders::g_SSAOStrength);
#else
    Shaders::RWImage2D_Heap_R8UNORM[u_PC.DstSSAOTextureID][DTid.xy] = pow(ao, Shaders::g_SSAOStrength);
#endif
}