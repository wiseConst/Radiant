// mip_build.slang

struct PushConstantBlock
{
    uint32_t samplerID;
    uint32_t imageID;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 gtid : SV_DispatchThreadID)
{

}