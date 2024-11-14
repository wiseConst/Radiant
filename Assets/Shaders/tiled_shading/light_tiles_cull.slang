// light_tiles_cull.slang

#include "../../../Source/ShaderDefines.hpp"
#include <light_tiles_defines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const Shaders::LightData *LightData;
};
[[vk::push_constant]] PushConstantBlock u_PC;

groupshared uint gs_MinDepth;
groupshared uint gs_MaxDepth;

[shader("compute")]
[numthreads(LIGHT_CULL_TILE_SIZE, LIGHT_CULL_TILE_SIZE, 1)]
void computeMain(const uint3 Gid : SV_GroupID, const uint3 DTid : SV_DispatchThreadID, const uint3 GTid : SV_GroupThreadID)
{
    if (WaveIsFirstLane())
    {
        gs_MinDepth = 0xFFFFFFFF;
        gs_MaxDepth = 0;
    }

    // one of them is extra i think
    GroupMemoryBarrierWithGroupSync();
    GroupMemoryBarrierWithWaveSync();
}