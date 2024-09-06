#include <pch.h>
#include "RenderGraph.hpp"

// NOTE: Only for RenderGraphResourcePool::UI_ShowResourceUsage()
#include <imgui.h>

template <> struct ankerl::unordered_dense::hash<vk::MemoryBarrier2>
{
    using is_avalanching = void;

    [[nodiscard]] auto operator()(const vk::MemoryBarrier2& x) const noexcept -> std::uint64_t
    {
        return detail::wyhash::hash(static_cast<std::uint64_t>(x.srcAccessMask) + static_cast<std::uint64_t>(x.srcStageMask) +
                                    static_cast<std::uint64_t>(x.dstAccessMask) + static_cast<std::uint64_t>(x.dstStageMask));
    }
};

namespace Radiant
{

    namespace RenderGraphUtils
    {

        static void DepthFirstSearch(u32 passID, std::vector<u32>& sortedPassID, const std::vector<std::vector<u32>>& adjacencyLists,
                                     std::vector<u8>& visitedPasses) noexcept
        {
            RDNT_ASSERT(passID < adjacencyLists.size() && passID < visitedPasses.size(), "Invalid passID!");

            visitedPasses[passID] = 1;
            for (auto otherPassID : adjacencyLists[passID])
            {
                RDNT_ASSERT(visitedPasses[otherPassID] != 1, "RenderGraph is not acyclic! Pass[{}] -> Pass[{}]", passID, otherPassID);

                if (visitedPasses[otherPassID] != 2) DepthFirstSearch(otherPassID, sortedPassID, adjacencyLists, visitedPasses);
            }

            sortedPassID.emplace_back(passID);
            visitedPasses[passID] = 2;
        }

        // HUGE NOTE:
        // Per-resource barriers should usually be used for queue ownership transfers and image layout transitions,
        // otherwise use global barriers.

        static void FillBufferBarrierIfNeeded(UnorderedSet<vk::MemoryBarrier2>& memoryBarriers,
                                              std::vector<vk::BufferMemoryBarrier2>& bufferMemoryBarriers, const Unique<GfxBuffer>& buffer,
                                              const ResourceStateFlags currentState, const ResourceStateFlags nextState) noexcept
        {
            // NOTE: BufferMemoryBarriers should be used only on queue ownership transfers.
            static constexpr bool s_bUseBufferMemoryBarriers = false;

            vk::PipelineStageFlags2 srcStageMask{vk::PipelineStageFlagBits2::eNone};
            vk::AccessFlags2 srcAccessMask{vk::AccessFlagBits2::eNone};
            vk::PipelineStageFlags2 dstStageMask{vk::PipelineStageFlagBits2::eNone};
            vk::AccessFlags2 dstAccessMask{vk::AccessFlagBits2::eNone};

            if (currentState == EResourceStateBits::RESOURCE_STATE_UNDEFINED) srcStageMask |= vk::PipelineStageFlagBits2::eBottomOfPipe;

            const bool bCurrentStateShaderResource = (currentState & (EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT |
                                                                      EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT |
                                                                      EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT));

            if (bCurrentStateShaderResource && currentState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
            {
                srcAccessMask |=
                    vk::AccessFlagBits2::eShaderRead;  // NOTE: This access implies both eShaderStorageRead & eShaderSampledRead
            }
            if (bCurrentStateShaderResource &&
                (currentState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT) == EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
            {
                srcAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            const bool bNextStateShaderResource = nextState & (EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT |
                                                               EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT |
                                                               EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
            if (bNextStateShaderResource && nextState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
            {
                dstAccessMask |=
                    vk::AccessFlagBits2::eShaderRead;  // NOTE: This access implies both eShaderStorageRead & eShaderSampledRead
            }
            if (bNextStateShaderResource && nextState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
            {
                dstAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            // CURRENT STATE
            if (currentState & EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT)
            {
                srcStageMask |= vk::PipelineStageFlagBits2::eComputeShader;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT)
            {
                srcStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_COPY_SOURCE_BIT)
            {
                // NOTE: Src copy buffer likes eTransferRead, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                srcAccessMask |= vk::AccessFlagBits2::eTransferRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_COPY_DESTINATION_BIT)
            {
                // NOTE: Dst copy buffer likes eTransferWrite, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderWrite;
                srcAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_INDEX_BUFFER_BIT)
            {
                srcAccessMask |= vk::AccessFlagBits2::eIndexRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eIndexInput;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_VERTEX_BUFFER_BIT ||
                currentState & EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT)
            {
                srcAccessMask |= vk::AccessFlagBits2::eMemoryRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT)
            {
                // NOTE: Uniform buffer likes eUniformRead, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                srcAccessMask |= vk::AccessFlagBits2::eUniformRead;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_INDIRECT_ARGUMENT_BIT)
            {
                // NOTE: Indirect arg buffer likes eIndirectCommandRead, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                srcAccessMask |= vk::AccessFlagBits2::eIndirectCommandRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eDrawIndirect;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT &&
                currentState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
            {
                srcAccessMask |= vk::AccessFlagBits2::eShaderRead;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT &&
                currentState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
            {
                srcAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            // NEXT STATE
            if (nextState & EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT)
            {
                dstStageMask |= vk::PipelineStageFlagBits2::eComputeShader;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_INDEX_BUFFER_BIT)
            {
                dstAccessMask |= vk::AccessFlagBits2::eIndexRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eIndexInput;
            }

            if (nextState &
                (EResourceStateBits::RESOURCE_STATE_VERTEX_BUFFER_BIT | EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT))
            {
                dstAccessMask |= vk::AccessFlagBits2::eMemoryRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_COPY_SOURCE_BIT)
            {
                // NOTE: Src copy buffer likes eTransferRead, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                dstAccessMask |= vk::AccessFlagBits2::eTransferRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_COPY_DESTINATION_BIT)
            {
                // NOTE: Dst copy buffer likes eTransferWrite, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderWrite;
                dstAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT)
            {
                // NOTE: Uniform buffer likes eUniformRead, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                dstAccessMask |= vk::AccessFlagBits2::eUniformRead;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_INDIRECT_ARGUMENT_BIT)
            {
                // NOTE: Indirect arg buffer likes eIndirectCommandRead, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                dstAccessMask |= vk::AccessFlagBits2::eIndirectCommandRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eDrawIndirect;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT)
            {
                dstStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT &&
                nextState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
            {
                dstAccessMask |= vk::AccessFlagBits2::eShaderRead;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT &&
                nextState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
            {
                dstAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            // NOTE: Read-To-Read don't need any sync.
            const bool bIsAnyWriteOpPresent =
                /*src*/ (srcAccessMask & vk::AccessFlagBits2::eShaderWrite) || (srcAccessMask & vk::AccessFlagBits2::eTransferWrite) ||
                (srcAccessMask & vk::AccessFlagBits2::eHostWrite) || (srcAccessMask & vk::AccessFlagBits2::eMemoryWrite) ||
                (srcAccessMask & vk::AccessFlagBits2::eTransferWrite) || (srcAccessMask & vk::AccessFlagBits2::eShaderStorageWrite) ||
                (srcAccessMask & vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                /*dst*/ (dstAccessMask & vk::AccessFlagBits2::eShaderWrite) || (dstAccessMask & vk::AccessFlagBits2::eTransferWrite) ||
                (dstAccessMask & vk::AccessFlagBits2::eHostWrite) || (dstAccessMask & vk::AccessFlagBits2::eMemoryWrite) ||
                (dstAccessMask & vk::AccessFlagBits2::eShaderStorageWrite) ||
                (dstAccessMask & vk::AccessFlagBits2::eAccelerationStructureWriteKHR);

            if (!bIsAnyWriteOpPresent) return;

            if (!s_bUseBufferMemoryBarriers)
                memoryBarriers.emplace(vk::MemoryBarrier2()
                                           .setSrcAccessMask(srcAccessMask)
                                           .setSrcStageMask(srcStageMask)
                                           .setDstAccessMask(dstAccessMask)
                                           .setDstStageMask(dstStageMask));
            else
                bufferMemoryBarriers.emplace_back()
                    .setBuffer(*buffer)
                    .setOffset(0)
                    .setSize(buffer->GetDescription().Capacity)
                    .setSrcAccessMask(srcAccessMask)
                    .setSrcStageMask(srcStageMask)
                    .setDstAccessMask(dstAccessMask)
                    .setDstStageMask(dstStageMask);
        }

        static void FillImageBarrierIfNeeded(UnorderedSet<vk::MemoryBarrier2>& memoryBarriers,
                                             std::vector<vk::ImageMemoryBarrier2>& imageMemoryBarriers, const Unique<GfxTexture>& texture,
                                             const ResourceStateFlags currentState, const ResourceStateFlags nextState,
                                             vk::ImageLayout& outNextLayout, const u32 subresourceIndex) noexcept
        {
            constexpr auto bestDepthStencilState =
                EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT | EResourceStateBits::RESOURCE_STATE_DEPTH_WRITE_BIT;

            vk::AccessFlags2 srcAccessMask{vk::AccessFlagBits2::eNone};
            vk::PipelineStageFlags2 srcStageMask{vk::PipelineStageFlagBits2::eNone};
            vk::ImageLayout oldLayout{vk::ImageLayout::eUndefined};

            vk::AccessFlags2 dstAccessMask{vk::AccessFlagBits2::eNone};
            vk::PipelineStageFlags2 dstStageMask{vk::PipelineStageFlagBits2::eNone};

            if (currentState == EResourceStateBits::RESOURCE_STATE_UNDEFINED) srcStageMask |= vk::PipelineStageFlagBits2::eBottomOfPipe;

            // CURRENT STATE
            if (currentState & EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT)
            {
                // NOTE: Tbh idk which way I should determine layout here but, my logic is that if you write to it, then it's eGeneral, but
                // if you only read it's eShaderReadOnlyOptimal, simple as that.
                if (currentState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
                {
                    oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                    // srcAccessMask |= vk::AccessFlagBits2::eShaderStorageRead;
                }

                if (currentState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
                {
                    oldLayout = vk::ImageLayout::eGeneral;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
                }

                srcStageMask |= vk::PipelineStageFlagBits2::eComputeShader;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT)
            {
                if (currentState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
                {
                    oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                }

                if (currentState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
                {
                    oldLayout = vk::ImageLayout::eGeneral;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
                }

                srcStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT)
            {
                oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_RENDER_TARGET_BIT)
            {
                oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
                if (currentState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
                {
                    srcAccessMask |= vk::AccessFlagBits2::eColorAttachmentRead;
                }

                if (currentState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
                {
                    srcAccessMask |= vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead;
                }

                srcStageMask |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT)
            {
                oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_DEPTH_WRITE_BIT)
            {
                oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_COPY_SOURCE_BIT)
            {
                oldLayout = vk::ImageLayout::eTransferSrcOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eTransferRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (currentState & EResourceStateBits::RESOURCE_STATE_COPY_DESTINATION_BIT)
            {
                oldLayout = vk::ImageLayout::eTransferDstOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            // NEXT STATE
            if (nextState & EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT)
            {
                if (nextState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
                {
                    if (currentState & EResourceStateBits::RESOURCE_STATE_RENDER_TARGET_BIT ||
                        currentState & EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT ||
                        currentState & EResourceStateBits::RESOURCE_STATE_DEPTH_WRITE_BIT)
                    {
                        outNextLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                        dstAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                    }

                    //   dstAccessMask |= vk::AccessFlagBits2::eShaderStorageRead;
                }

                if (nextState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
                {
                    outNextLayout = vk::ImageLayout::eGeneral;
                    dstAccessMask |= vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
                }

                // NOTE: In case we failed to determine next layout, fallback to eShaderReadOnlyOptimal, cuz first if-statement is weak.
                if (outNextLayout == vk::ImageLayout::eUndefined)
                {
                    outNextLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    dstAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                }

                dstStageMask |= vk::PipelineStageFlagBits2::eComputeShader;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT)
            {
                if (nextState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
                {
                    outNextLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    dstAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                }

                if (nextState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
                {
                    outNextLayout = vk::ImageLayout::eGeneral;
                    dstAccessMask |= vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
                }

                dstStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT)
            {
                outNextLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;

                // NOTE: Wait for previous depth ops to be finished on this resource.
                if (oldLayout == vk::ImageLayout::eUndefined)
                {
                    srcAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                    srcStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
                }
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_DEPTH_WRITE_BIT)
            {
                outNextLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;

                // NOTE: Wait for previous depth ops to be finished on this resource.
                if (oldLayout == vk::ImageLayout::eUndefined)
                {
                    srcAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                    srcStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
                }
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_RENDER_TARGET_BIT)
            {
                outNextLayout = vk::ImageLayout::eColorAttachmentOptimal;
                if (nextState & EResourceStateBits::RESOURCE_STATE_READ_BIT)
                {
                    dstAccessMask |= vk::AccessFlagBits2::eColorAttachmentRead;
                }

                if (nextState & EResourceStateBits::RESOURCE_STATE_WRITE_BIT)
                {
                    dstAccessMask |= vk::AccessFlagBits2::eColorAttachmentWrite;
                }

                dstStageMask |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT)
            {
                outNextLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_COPY_SOURCE_BIT)
            {
                outNextLayout = vk::ImageLayout::eTransferSrcOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eTransferRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (nextState & EResourceStateBits::RESOURCE_STATE_COPY_DESTINATION_BIT)
            {
                outNextLayout = vk::ImageLayout::eTransferDstOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (oldLayout == outNextLayout && oldLayout == vk::ImageLayout::eUndefined || outNextLayout == vk::ImageLayout::eUndefined)
                RDNT_ASSERT(false, "Failed to determine image barrier!");

            // NOTE: Read-To-Read don't need any sync.
            const bool bIsAnyWriteOpPresent =
                /*src*/ (srcAccessMask & vk::AccessFlagBits2::eShaderWrite) || (srcAccessMask & vk::AccessFlagBits2::eTransferWrite) ||
                (srcAccessMask & vk::AccessFlagBits2::eHostWrite) || (srcAccessMask & vk::AccessFlagBits2::eMemoryWrite) ||
                (srcAccessMask & vk::AccessFlagBits2::eTransferWrite) || (srcAccessMask & vk::AccessFlagBits2::eShaderStorageWrite) ||
                (srcAccessMask & vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                /*dst*/ (dstAccessMask & vk::AccessFlagBits2::eShaderWrite) || (dstAccessMask & vk::AccessFlagBits2::eTransferWrite) ||
                (dstAccessMask & vk::AccessFlagBits2::eHostWrite) || (dstAccessMask & vk::AccessFlagBits2::eMemoryWrite) ||
                (dstAccessMask & vk::AccessFlagBits2::eShaderStorageWrite) ||
                (dstAccessMask & vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
            // NOTE: Read-To-Read don't need any sync, but we can't skip image transitions!
            if (oldLayout == outNextLayout)
            {
                if (bIsAnyWriteOpPresent)
                    memoryBarriers.emplace(vk::MemoryBarrier2()
                                               .setSrcAccessMask(srcAccessMask)
                                               .setSrcStageMask(srcStageMask)
                                               .setDstAccessMask(dstAccessMask)
                                               .setDstStageMask(dstStageMask));
            }
            else
            {
                auto& imageMemoryBarrier =
                    imageMemoryBarriers.emplace_back(srcStageMask, srcAccessMask, dstStageMask, dstAccessMask, oldLayout, outNextLayout,
                                                     vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *texture);

                imageMemoryBarrier.setSubresourceRange(vk::ImageSubresourceRange()
                                                           .setBaseArrayLayer(0)
                                                           .setLayerCount(1)
                                                           .setAspectMask(texture->IsDepthFormat(texture->GetDescription().Format)
                                                                              ? vk::ImageAspectFlagBits::eDepth
                                                                              : vk::ImageAspectFlagBits::eColor)
                                                           .setBaseMipLevel(subresourceIndex)
                                                           .setLevelCount(1));
            }
        }

    }  // namespace RenderGraphUtils

    void RenderGraph::AddPass(const std::string_view& name, const ERenderGraphPassType passType, RenderGraphSetupFunc&& setupFunc,
                              RenderGraphExecuteFunc&& executeFunc) noexcept
    {
        auto& pass = m_Passes.emplace_back(MakeUnique<RenderGraphPass>(static_cast<u32>(m_Passes.size()), name, passType,
                                                                       std::forward<RenderGraphSetupFunc>(setupFunc),
                                                                       std::forward<RenderGraphExecuteFunc>(executeFunc)));
        RenderGraphResourceScheduler scheduler(*this, *pass);
        pass->Setup(scheduler);
    }

    void RenderGraph::Build() noexcept
    {
        RDNT_ASSERT(!m_Passes.empty(), "RenderGraph is empty!");
        m_Passes.shrink_to_fit();

        const auto buildBeginTime = Timer::Now();

        BuildAdjacencyLists();
        TopologicalSort();
        BuildDependencyLevels();

        GraphvizDump();

        m_Stats.BuildTime = std::chrono::duration<f32, std::chrono::milliseconds::period>(Timer::Now() - buildBeginTime).count();
    }

    void RenderGraph::BuildAdjacencyLists() noexcept
    {
        m_AdjacencyLists.resize(m_Passes.size());

        for (const auto& writePass : m_Passes)
        {
            for (const auto& readPass : m_Passes)
            {
                // Skip self.
                if (writePass->m_ID == readPass->m_ID) continue;

                bool bAnyDependencyFound = false;
                for (const auto& subresourceID : writePass->m_TextureWrites)
                {
                    bAnyDependencyFound = std::find(readPass->m_TextureReads.cbegin(), readPass->m_TextureReads.cend(), subresourceID) !=
                                          readPass->m_TextureReads.cend();

                    if (bAnyDependencyFound) break;
                }
                if (bAnyDependencyFound)
                {
                    m_AdjacencyLists[writePass->m_ID].emplace_back(readPass->m_ID);
                    continue;
                }

                for (const auto& subresourceID : writePass->m_BufferWrites)
                {
                    bAnyDependencyFound = std::find(readPass->m_BufferReads.cbegin(), readPass->m_BufferReads.cend(), subresourceID) !=
                                          readPass->m_BufferReads.cend();

                    if (bAnyDependencyFound) break;
                }
                if (bAnyDependencyFound) m_AdjacencyLists[writePass->m_ID].emplace_back(readPass->m_ID);
            }
            m_AdjacencyLists[writePass->m_ID].shrink_to_fit();
        }
    }

    void RenderGraph::TopologicalSort() noexcept
    {
        std::vector<u8> visitedPasses(m_Passes.size(), 0);

        m_TopologicallySortedPassesID.reserve(m_Passes.size());
        for (const auto& pass : m_Passes)
        {
            if (visitedPasses[pass->m_ID] != 2)
                RenderGraphUtils::DepthFirstSearch(pass->m_ID, m_TopologicallySortedPassesID, m_AdjacencyLists, visitedPasses);
        }

        std::ranges::reverse(m_TopologicallySortedPassesID);
    }

    void RenderGraph::BuildDependencyLevels() noexcept
    {
        std::vector<u32> longestPassDistances(m_TopologicallySortedPassesID.size(), 0);
        u32 dependencyLevelCount{1};

        // 1. Perform longest distance(from root node) search for each node.
        for (const auto node : m_TopologicallySortedPassesID)
        {
            for (const auto adjacentNode : m_AdjacencyLists[node])
            {
                if (longestPassDistances[adjacentNode] >= longestPassDistances[node] + 1) continue;

                const auto newLongestDistance      = longestPassDistances[node] + 1;
                longestPassDistances[adjacentNode] = newLongestDistance;
                dependencyLevelCount               = std::max(newLongestDistance + 1, dependencyLevelCount);
            }
        }

        // 2. Fill dependency levels.
        // Dispatch nodes to corresponding dependency levels.
        // Iterate through unordered nodes because adjacency lists contain indices to
        // initial unordered list of nodes and longest distances also correspond to them.
        m_DependencyLevels.resize(dependencyLevelCount, *this);
        for (u32 passIndex{0}; passIndex < m_Passes.size(); ++passIndex)
        {
            const auto levelIndex        = longestPassDistances[passIndex];
            auto& dependencyLevel        = m_DependencyLevels[levelIndex];
            dependencyLevel.m_LevelIndex = levelIndex;
            dependencyLevel.AddPass(m_Passes[passIndex].get());

            m_Passes[passIndex]->m_DependencyLevelIndex = levelIndex;
        }
    }

    void RenderGraph::CreateResources() noexcept
    {
        for (auto& [textureName, textureDesc] : m_TextureCreates)
        {
            textureDesc.bControlledByRenderGraph    = s_bUseResourceMemoryAliasing;
            const auto resourceID                   = GetResourceID(textureName);
            const auto resourceHandle               = m_ResourcePool->CreateTexture(textureDesc, textureName, resourceID);
            m_ResourceIDToTextureHandle[resourceID] = resourceHandle;

            if constexpr (s_bUseResourceMemoryAliasing)
            {
                auto& gfxTextureHandle = m_ResourcePool->GetTexture(m_ResourceIDToTextureHandle[resourceID])->Get();
                m_ResourcePool->FillResourceInfo(
                    resourceHandle, resourceID, textureName,
                    m_GfxContext->GetDevice()->GetLogicalDevice()->getImageMemoryRequirements(*gfxTextureHandle),
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
            }
        }

        for (auto& [bufferName, bufferDesc] : m_BufferCreates)
        {
            bufferDesc.bControlledByRenderGraph    = s_bUseResourceMemoryAliasing;
            const auto resourceID                  = GetResourceID(bufferName);
            const auto resourceHandle              = m_ResourcePool->CreateBuffer(bufferDesc, bufferName, resourceID);
            m_ResourceIDToBufferHandle[resourceID] = resourceHandle;

            if constexpr (s_bUseResourceMemoryAliasing)
            {
                vk::MemoryPropertyFlags memoryPropertyFlags{};
                if (bufferDesc.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)
                    memoryPropertyFlags |= vk::MemoryPropertyFlagBits::eDeviceLocal;

                if (bufferDesc.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT)
                    memoryPropertyFlags |= vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                auto& gfxBufferHandle = m_ResourcePool->GetBuffer(m_ResourceIDToBufferHandle[resourceID])->Get();
                m_ResourcePool->FillResourceInfo(
                    resourceHandle, resourceID, bufferName,
                    m_GfxContext->GetDevice()->GetLogicalDevice()->getBufferMemoryRequirements(*gfxBufferHandle), memoryPropertyFlags);
            }
        }

        if constexpr (s_bUseResourceMemoryAliasing)
        {
            m_ResourcePool->CalculateEffectiveLifetimes(m_TopologicallySortedPassesID, m_ResourcesUsedByPassesID);
            m_ResourcePool->BindResourcesToMemoryRegions();
        }

        m_TextureCreates.clear();
        m_BufferCreates.clear();
    }

    void RenderGraph::Execute() noexcept
    {
        RDNT_ASSERT(!m_TopologicallySortedPassesID.empty(), "RenderGraph isn't built!");

        CreateResources();

        const auto& frameData = m_GfxContext->GetCurrentFrameData();
        frameData.GeneralCommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        const auto& pipelineLayout    = *m_GfxContext->GetDevice()->GetBindlessPipelineLayout();
        const auto& bindlessResources = m_GfxContext->GetDevice()->GetCurrentFrameBindlessResources();
        frameData.GeneralCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                                                          bindlessResources.DescriptorSet, {});
        frameData.GeneralCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
                                                          bindlessResources.DescriptorSet, {});

        // NOTE: Firstly reserve enough space for timestamps
        if (frameData.TimestampsCapacity < m_Passes.size() * 2)
        {
            if (frameData.TimestampsQueryPool)
                m_GfxContext->GetDevice()->PushObjectToDelete(
                    [movedTimestampsQueryPool = std::move(frameData.TimestampsQueryPool)]() noexcept {});

            frameData.TimestampsCapacity  = m_Passes.size() * 2;  // *2 since it works so (begin + end)
            frameData.TimestampsQueryPool = m_GfxContext->GetDevice()->GetLogicalDevice()->createQueryPoolUnique(
                vk::QueryPoolCreateInfo().setQueryType(vk::QueryType::eTimestamp).setQueryCount(frameData.TimestampsCapacity));
            m_GfxContext->GetDevice()->GetLogicalDevice()->resetQueryPool(*frameData.TimestampsQueryPool, 0, frameData.TimestampsCapacity);
        }

        for (auto& dependencyLevel : m_DependencyLevels)
        {
            dependencyLevel.Execute(m_GfxContext);
        }

        frameData.GeneralCommandBuffer.end();

        // NOTE: In future I might upscale(compute) or load into swapchain image or render into so here's optimal flags.
        const vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                                        vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader |
                                                        vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                                        vk::PipelineStageFlagBits::eLateFragmentTests;

        const auto& presentQueue = m_GfxContext->GetDevice()->GetPresentQueue().Handle;
        presentQueue.submit(vk::SubmitInfo()
                                .setCommandBuffers(frameData.GeneralCommandBuffer)
                                .setSignalSemaphores(*frameData.RenderFinishedSemaphore)
                                .setWaitSemaphores(*frameData.ImageAvailableSemaphore)
                                .setWaitDstStageMask(waitDstStageMask),
                            *frameData.RenderFinishedFence);
    }

    void RenderGraph::DependencyLevel::Execute(const Unique<GfxContext>& gfxContext) noexcept
    {
        TransitionResourceStates(gfxContext);

        auto& frameData = gfxContext->GetCurrentFrameData();
        auto& cmd       = frameData.GeneralCommandBuffer;

        for (auto& currentPass : m_Passes)
        {
#if RDNT_DEBUG
            cmd.beginDebugUtilsLabelEXT(
                vk::DebugUtilsLabelEXT().setPLabelName(currentPass->m_Name.data()).setColor({1.0f, 1.0f, 1.0f, 1.0f}));
#endif

            auto& gpuTask = frameData.GPUProfilerData.emplace_back();
            gpuTask.Name  = currentPass->m_Name;
            gpuTask.Color = Colors::ColorArray[currentPass->m_ID % Colors::ColorArray.size()];

            // NOTE: https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/api/hpp_timestamp_queries#writing-time-stamps
            // Calling this function defines an execution dependency similar to barrier on all commands that were submitted before it!
            cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *frameData.TimestampsQueryPool, frameData.CurrentTimestampIndex++);

            auto& cpuTask     = frameData.CPUProfilerData.emplace_back();
            cpuTask.StartTime = Timer::GetElapsedSecondsFromNow(frameData.FrameStartTime);
            cpuTask.Name      = currentPass->m_Name;
            cpuTask.Color     = Colors::ColorArray[currentPass->m_ID % Colors::ColorArray.size()];

            if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
            {
                RDNT_ASSERT(currentPass->m_Viewport.has_value(), "Viewport is invalid!");
                RDNT_ASSERT(currentPass->m_Scissor.has_value(), "Scissor is invalid!");
            }

            // TODO: Fill stencil
            auto stencilAttachmentInfo = vk::RenderingAttachmentInfo();
            auto depthAttachmentInfo   = vk::RenderingAttachmentInfo();
            std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos;
            u32 layerCount{1};

            for (const auto& subresourceID : currentPass->m_TextureReads)
            {
                auto& RGtexture =
                    m_RenderGraph.m_ResourcePool->GetTexture(m_RenderGraph.m_ResourceIDToTextureHandle[subresourceID.ResourceID]);
                auto& texture = RGtexture->Get();

                if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
                {
                    const auto nextState = currentPass->m_ResourceIDToResourceState[subresourceID];

                    // NOTE: Since vulkan allows writing to storage texture from fragment shader we should take that into account
                    // NOTE: In case we use attachment as read only, other not supported!
                    const bool bIsRasterUsage = (nextState & EResourceStateBits::RESOURCE_STATE_RENDER_TARGET_BIT) ||
                                                (nextState & EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT) ||
                                                (nextState & EResourceStateBits::RESOURCE_STATE_DEPTH_WRITE_BIT);
                    if (!bIsRasterUsage) continue;

                    layerCount = std::max(layerCount, texture->GetDescription().LayerCount);
                    if (texture->IsDepthFormat(texture->GetDescription().Format))
                    {
                        depthAttachmentInfo = texture->GetRenderingAttachmentInfo(
                            vk::ImageLayout::eDepthStencilAttachmentOptimal, {}, vk::AttachmentLoadOp::eLoad,
                            vk::AttachmentStoreOp::eDontCare, subresourceID.SubresourceIndex);
                    }
                    else
                    {
                        colorAttachmentInfos.emplace_back() =
                            texture->GetRenderingAttachmentInfo(vk::ImageLayout::eColorAttachmentOptimal, {}, vk::AttachmentLoadOp::eLoad,
                                                                vk::AttachmentStoreOp::eDontCare, subresourceID.SubresourceIndex);
                    }
                }
            }

            for (const auto& subresourceID : currentPass->m_TextureWrites)
            {
                auto& RGtexture =
                    m_RenderGraph.m_ResourcePool->GetTexture(m_RenderGraph.m_ResourceIDToTextureHandle[subresourceID.ResourceID]);
                auto& texture = RGtexture->Get();

                if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
                {
                    const auto nextState = currentPass->m_ResourceIDToResourceState[subresourceID];

                    // NOTE: Since vulkan allows writing to storage texture from fragment shader we should take that into account
                    // NOTE: In case we use attachment as read only, other not supported!
                    const bool bIsRasterUsage = (nextState & EResourceStateBits::RESOURCE_STATE_RENDER_TARGET_BIT) ||
                                                (nextState & EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT) ||
                                                (nextState & EResourceStateBits::RESOURCE_STATE_DEPTH_WRITE_BIT);
                    if (!bIsRasterUsage) continue;

                    layerCount = std::max(layerCount, texture->GetDescription().LayerCount);
                    if (texture->IsDepthFormat(texture->GetDescription().Format))
                    {
                        depthAttachmentInfo = texture->GetRenderingAttachmentInfo(
                            vk::ImageLayout::eDepthStencilAttachmentOptimal,
                            vk::ClearValue().setDepthStencil(*currentPass->m_DepthStencilInfo->ClearValue),
                            currentPass->m_DepthStencilInfo->DepthLoadOp, currentPass->m_DepthStencilInfo->DepthStoreOp,
                            subresourceID.SubresourceIndex);
                    }
                    else
                    {
                        auto& currentRTInfo                 = currentPass->m_RenderTargetInfos[colorAttachmentInfos.size()];
                        colorAttachmentInfos.emplace_back() = texture->GetRenderingAttachmentInfo(
                            vk::ImageLayout::eColorAttachmentOptimal, vk::ClearValue().setColor(*currentRTInfo.ClearValue),
                            currentRTInfo.LoadOp, currentRTInfo.StoreOp, subresourceID.SubresourceIndex);
                    }
                }
            }

            if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS &&
                (currentPass->m_DepthStencilInfo.has_value() || !currentPass->m_RenderTargetInfos.empty()))
            {
                cmd.beginRendering(
                    vk::RenderingInfo()
                        .setColorAttachments(colorAttachmentInfos)
                        .setLayerCount(layerCount)
                        .setPDepthAttachment(&depthAttachmentInfo)
                        .setPStencilAttachment(&stencilAttachmentInfo)
                        .setRenderArea(vk::Rect2D().setExtent(
                            vk::Extent2D().setWidth(currentPass->m_Viewport->width).setHeight(currentPass->m_Viewport->height))));
            }

            RenderGraphResourceScheduler scheduler(m_RenderGraph, *currentPass);
            currentPass->Execute(scheduler, cmd);

            if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS &&
                (currentPass->m_DepthStencilInfo.has_value() || !currentPass->m_RenderTargetInfos.empty()))
                cmd.endRendering();

            cpuTask.EndTime = Timer::GetElapsedSecondsFromNow(frameData.FrameStartTime);
            cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *frameData.TimestampsQueryPool,
                                frameData.CurrentTimestampIndex++);

#if RDNT_DEBUG
            cmd.endDebugUtilsLabelEXT();
#endif
        }
    }

    void RenderGraph::DependencyLevel::TransitionResourceStates(const Unique<GfxContext>& gfxContext) noexcept
    {
        auto& frameData = gfxContext->GetCurrentFrameData();
        auto& cmd       = frameData.GeneralCommandBuffer;

        std::vector<vk::ImageMemoryBarrier2> imageMemoryBarriers;
        std::vector<vk::BufferMemoryBarrier2> bufferMemoryBarriers;
        UnorderedSet<vk::MemoryBarrier2> memoryBarriers;

        for (auto& currentPass : m_Passes)
        {
            for (const auto& subresourceID : currentPass->m_BufferReads)
            {
                auto& RGbuffer =
                    m_RenderGraph.m_ResourcePool->GetBuffer(m_RenderGraph.m_ResourceIDToBufferHandle[subresourceID.ResourceID]);
                auto& buffer = RGbuffer->Get();

                const auto currentState = RGbuffer->GetState();
                const auto nextState    = currentPass->m_ResourceIDToResourceState[subresourceID];

                RenderGraphUtils::FillBufferBarrierIfNeeded(memoryBarriers, bufferMemoryBarriers, buffer, currentState, nextState);
                RGbuffer->SetState(nextState);
            }

            for (const auto& subresourceID : currentPass->m_BufferWrites)
            {
                auto& RGbuffer =
                    m_RenderGraph.m_ResourcePool->GetBuffer(m_RenderGraph.m_ResourceIDToBufferHandle[subresourceID.ResourceID]);
                auto& buffer = RGbuffer->Get();

                const auto currentState = RGbuffer->GetState();
                const auto nextState    = currentPass->m_ResourceIDToResourceState[subresourceID];

                RenderGraphUtils::FillBufferBarrierIfNeeded(memoryBarriers, bufferMemoryBarriers, buffer, currentState, nextState);
                RGbuffer->SetState(nextState);
            }

            for (const auto& subresourceID : currentPass->m_TextureReads)
            {
                auto& RGtexture =
                    m_RenderGraph.m_ResourcePool->GetTexture(m_RenderGraph.m_ResourceIDToTextureHandle[subresourceID.ResourceID]);
                auto& texture = RGtexture->Get();

                const auto currentState = RGtexture->GetState(subresourceID.SubresourceIndex);
                const auto nextState    = currentPass->m_ResourceIDToResourceState[subresourceID];

                vk::ImageLayout nextLayout{vk::ImageLayout::eUndefined};
                RenderGraphUtils::FillImageBarrierIfNeeded(memoryBarriers, imageMemoryBarriers, texture, currentState, nextState,
                                                           nextLayout, subresourceID.SubresourceIndex);
                RGtexture->SetState(nextState, subresourceID.SubresourceIndex);
            }

            for (const auto& subresourceID : currentPass->m_TextureWrites)
            {
                auto& RGtexture =
                    m_RenderGraph.m_ResourcePool->GetTexture(m_RenderGraph.m_ResourceIDToTextureHandle[subresourceID.ResourceID]);
                auto& texture = RGtexture->Get();

                const auto currentState = RGtexture->GetState(subresourceID.SubresourceIndex);
                const auto nextState    = currentPass->m_ResourceIDToResourceState[subresourceID];

                vk::ImageLayout nextLayout{vk::ImageLayout::eUndefined};
                RenderGraphUtils::FillImageBarrierIfNeeded(memoryBarriers, imageMemoryBarriers, texture, currentState, nextState,
                                                           nextLayout, subresourceID.SubresourceIndex);
                RGtexture->SetState(nextState, subresourceID.SubresourceIndex);
            }
        }

        std::vector<vk::MemoryBarrier2> memoryBarrierVector{memoryBarriers.begin(), memoryBarriers.end()};
        if (!memoryBarrierVector.empty() || !bufferMemoryBarriers.empty() || !imageMemoryBarriers.empty())
        {
            cmd.pipelineBarrier2(vk::DependencyInfo()
                                     .setMemoryBarriers(memoryBarrierVector)
                                     .setBufferMemoryBarriers(bufferMemoryBarriers)
                                     .setImageMemoryBarriers(imageMemoryBarriers));

            ++m_RenderGraph.m_Stats.BarrierBatchCount;
            m_RenderGraph.m_Stats.BarrierCount += memoryBarrierVector.size() + bufferMemoryBarriers.size() + imageMemoryBarriers.size();
        }
    }

    NODISCARD Unique<GfxTexture>& RenderGraph::GetTexture(const RGResourceID& resourceID) noexcept
    {
        RDNT_ASSERT(m_ResourceIDToTextureHandle.contains(resourceID), "ResourceID isn't present in ResourceIDToTextureHandle map!");
        return m_ResourcePool->GetTexture(m_ResourceIDToTextureHandle[resourceID])->Get();
    }

    NODISCARD Unique<GfxBuffer>& RenderGraph::GetBuffer(const RGResourceID& resourceID) noexcept
    {
        RDNT_ASSERT(m_ResourceIDToBufferHandle.contains(resourceID), "ResourceID isn't present in ResourceIDToBufferHandle map!");
        return m_ResourcePool->GetBuffer(m_ResourceIDToBufferHandle[resourceID])->Get();
    }

    void RenderGraph::GraphvizDump() const noexcept
    {
        RDNT_ASSERT(!m_Passes.empty() && !m_Name.empty(), "DebugName or passes array is not valid!");

        std::stringstream ss;
        ss << "digraph " << m_Name << " {" << std::endl;
        ss << "\tnode [shape=rectangle, style=filled];" << std::endl;
        ss << "\tedge [color=black];" << std::endl << std::endl;

        for (const auto passIndex : m_TopologicallySortedPassesID)
        {
            const auto& pass = m_Passes[passIndex];
            for (const auto passIndex : m_AdjacencyLists[passIndex])
            {
                ss << "\t" << pass->m_Name << " -> " << m_Passes[passIndex]->m_Name << std::endl;
            }
            ss << std::endl;
        }
        ss << "}" << std::endl;

        CoreUtils::SaveData("render_graph_ref.dot", ss);
    }

    void RenderGraphResourceScheduler::CreateBuffer(const std::string& name, const GfxBufferDescription& bufferDesc) noexcept
    {
        const auto resourceID                             = m_RenderGraph.CreateResourceID(name);
        const auto subresourceID                          = RenderGraphSubresourceID(name, resourceID, 0);
        m_RenderGraph.m_BufferCreates[name]               = bufferDesc;
        m_Pass.m_ResourceIDToResourceState[subresourceID] = EResourceStateBits::RESOURCE_STATE_UNDEFINED;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::ReadBuffer(const std::string& name,
                                                                    const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID    = m_RenderGraph.GetResourceID(name);
        const auto subresourceID = RenderGraphSubresourceID(name, resourceID, 0);
        m_Pass.m_BufferReads.emplace_back(subresourceID);
        m_Pass.m_ResourceIDToResourceState[subresourceID] |= resourceState | EResourceStateBits::RESOURCE_STATE_READ_BIT;
        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::WriteBuffer(const std::string& name,
                                                                     const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID    = m_RenderGraph.GetResourceID(name);
        const auto subresourceID = RenderGraphSubresourceID(name, resourceID, 0);
        m_Pass.m_BufferWrites.emplace_back(subresourceID);
        m_Pass.m_ResourceIDToResourceState[subresourceID] |= resourceState | EResourceStateBits::RESOURCE_STATE_WRITE_BIT;
        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    void RenderGraphResourceScheduler::WriteDepthStencil(const std::string& name, const MipSet& mipSet,
                                                         const vk::AttachmentLoadOp depthLoadOp, const vk::AttachmentStoreOp depthStoreOp,
                                                         const vk::ClearDepthStencilValue& clearValue,
                                                         const vk::AttachmentLoadOp stencilLoadOp,
                                                         const vk::AttachmentStoreOp stencilStoreOp,
                                                         const std::string& newAliasName) noexcept
    {
        const auto resourceID = WriteTexture(
            name, mipSet, EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT | EResourceStateBits::RESOURCE_STATE_DEPTH_WRITE_BIT,
            newAliasName);
        m_Pass.m_DepthStencilInfo = {.ClearValue     = clearValue,
                                     .DepthLoadOp    = depthLoadOp,
                                     .DepthStoreOp   = depthStoreOp,
                                     .StencilLoadOp  = stencilLoadOp,
                                     .StencilStoreOp = stencilStoreOp};
        (void)resourceID;
    }

    void RenderGraphResourceScheduler::WriteRenderTarget(const std::string& name, const MipSet& mipSet, const vk::AttachmentLoadOp loadOp,
                                                         const vk::AttachmentStoreOp storeOp, const vk::ClearColorValue& clearValue,
                                                         const std::string& newAliasName) noexcept
    {
        const auto resourceID = WriteTexture(name, mipSet, EResourceStateBits::RESOURCE_STATE_RENDER_TARGET_BIT, newAliasName);
        m_Pass.m_RenderTargetInfos.emplace_back(clearValue, loadOp, storeOp);
        (void)resourceID;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::ReadTexture(const std::string& name, const MipSet& mipSet,
                                                                     const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);

        u32 mipLevelCount{1};
        u32 baseMipLevel{0};
        if (mipSet.Combination.has_value())
        {
            const auto& mipVariant = *mipSet.Combination;

            if (const auto* mipLevel = std::get_if<u32>(&mipVariant))
            {
                baseMipLevel = *mipLevel;
                if (baseMipLevel == std::numeric_limits<u32>::max())  // Last mip case
                    baseMipLevel = m_RenderGraph.GetTextureMipCount(name) - 1;
            }
            else if (const auto* mipRange = std::get_if<MipRange>(&mipVariant))
            {
                baseMipLevel = mipRange->first;
                if (mipRange->second.has_value())
                    mipLevelCount = *mipRange->second - baseMipLevel + 1;
                else
                    mipLevelCount = m_RenderGraph.GetTextureMipCount(name) - baseMipLevel + 1;
            }
            else
                RDNT_ASSERT(false, "Unknown MipVariant!");
        }

        for (u32 p = baseMipLevel; p < baseMipLevel + mipLevelCount; ++p)
        {
            const auto subresourceID = RenderGraphSubresourceID(name, resourceID, p);
            m_Pass.m_TextureReads.emplace_back(subresourceID);
            m_Pass.m_ResourceIDToResourceState[subresourceID] |= resourceState | EResourceStateBits::RESOURCE_STATE_READ_BIT;
        }

        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::WriteTexture(const std::string& name, const MipSet& mipSet,
                                                                      const ResourceStateFlags resourceState,
                                                                      const std::string& newAliasName) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);
        if (newAliasName != s_DEFAULT_STRING)
        {
            RDNT_ASSERT(!m_RenderGraph.m_ResourceAliasMap.contains(newAliasName), "Alias to Resource[{}] already exists!", name);
            m_RenderGraph.m_ResourceAliasMap[newAliasName] = name;
        }

        u32 mipLevelCount{1};
        u32 baseMipLevel{0};
        if (mipSet.Combination.has_value())
        {
            const auto& mipVariant = *mipSet.Combination;

            if (const auto* mipLevel = std::get_if<u32>(&mipVariant))
            {
                baseMipLevel = *mipLevel;
                if (baseMipLevel == std::numeric_limits<u32>::max())  // Last mip case
                    baseMipLevel = m_RenderGraph.GetTextureMipCount(name) - 1;
            }
            else if (const auto* mipRange = std::get_if<MipRange>(&mipVariant))
            {
                baseMipLevel = mipRange->first;
                if (mipRange->second.has_value())
                    mipLevelCount = *mipRange->second - baseMipLevel + 1;
                else
                    mipLevelCount = m_RenderGraph.GetTextureMipCount(name) - baseMipLevel + 1;
            }
            else
                RDNT_ASSERT(false, "Unknown MipVariant!");
        }

        for (u32 p = baseMipLevel; p < baseMipLevel + mipLevelCount; ++p)
        {
            const auto subresourceID = RenderGraphSubresourceID(newAliasName != s_DEFAULT_STRING ? newAliasName : name, resourceID, p);
            m_Pass.m_TextureWrites.emplace_back(subresourceID);
            m_Pass.m_ResourceIDToResourceState[subresourceID] |=
                resourceState | EResourceStateBits::RESOURCE_STATE_WRITE_BIT | EResourceStateBits::RESOURCE_STATE_READ_BIT;
        }

        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    void RenderGraphResourceScheduler::CreateTexture(const std::string& name, const GfxTextureDescription& textureDesc) noexcept
    {
        const auto resourceID                             = m_RenderGraph.CreateResourceID(name);
        const auto subresourceID                          = RenderGraphSubresourceID(name, resourceID, 0);
        m_RenderGraph.m_TextureCreates[name]              = textureDesc;
        m_Pass.m_ResourceIDToResourceState[subresourceID] = EResourceStateBits::RESOURCE_STATE_UNDEFINED;
    }

    void RenderGraphResourcePool::UI_ShowResourceUsage() const noexcept
    {
        if (ImGui::TreeNodeEx("RenderGraphResourcePool Statistics", ImGuiTreeNodeFlags_Framed))
        {
            if constexpr (!s_bUseResourceMemoryAliasing)
            {
                ImGui::Text("s_bUseResourceMemoryAliasing is false!");
                ImGui::TreePop();
                return;
            }

            const auto DrawMemoryAliaserStatisticsFunc = [](const std::string& rmaName, const auto& rma)
            {
                if (ImGui::TreeNodeEx(rmaName.data(), ImGuiTreeNodeFlags_Framed))
                {
                    ImGui::Text("Memory Buckets: %u", rma.m_MemoryBuckets.size());
                    ImGui::Separator();

                    for (u32 memoryBucketIndex{}; memoryBucketIndex < rma.m_MemoryBuckets.size(); ++memoryBucketIndex)
                    {
                        const auto& currentMemoryBucket = rma.m_MemoryBuckets[memoryBucketIndex];
                        u64 totalMemoryUsage{0};
                        for (const auto& resource : currentMemoryBucket.AlreadyAliasedResources)
                            totalMemoryUsage += resource.MemoryRequirements.size;

                        const float memoryReduction =
                            (totalMemoryUsage - currentMemoryBucket.MemoryRequirements.size) / static_cast<f32>(totalMemoryUsage) * 100.0f;

                        std::ostringstream memoryBucketName;
                        memoryBucketName << "ResourceBucket[" << memoryBucketIndex << std::setprecision(2)
                                         << "], Size: " << currentMemoryBucket.MemoryRequirements.size / 1024.f / 1024.f
                                         << "MB, Reduction: " << memoryReduction << "%.";
                        if (ImGui::TreeNodeEx(memoryBucketName.str().data(), ImGuiTreeNodeFlags_Framed))
                        {
                            for (const auto& resource : currentMemoryBucket.AlreadyAliasedResources)
                            {
                                ImGui::Text("Resource[ %s ], ResourceID[ %llu ], Offset[ %0.3f ] MB, Size[ %0.3f ] MB.",
                                            resource.DebugName.data(), resource.ID, resource.Offset / 1024.f / 1024.f,
                                            resource.MemoryRequirements.size / 1024.f / 1024.f);
                            }

                            ImGui::TreePop();
                        }
                    }

                    ImGui::TreePop();
                }
            };

            DrawMemoryAliaserStatisticsFunc("Device Resource Memory Aliaser", m_DeviceRMA);
            DrawMemoryAliaserStatisticsFunc("ReBAR Resource Memory Aliaser", m_ReBARRMA[m_CurrentFrameIndex]);
            DrawMemoryAliaserStatisticsFunc("Host Resource Memory Aliaser", m_HostRMA[m_CurrentFrameIndex]);

            ImGui::TreePop();
        }
    }

    NODISCARD RGTextureHandle RenderGraphResourcePool::CreateTexture(const GfxTextureDescription& textureDesc,
                                                                     const std::string& textureName,
                                                                     const RGResourceID& resourceID) noexcept
    {
        const auto SetTextureDebugNameFunc = [&](const std::string& textureName, const vk::Image& image)
        { m_Device->SetDebugName(textureName, image); };

        RGTextureHandle handleID{0};
        for (auto& [RGTexture, lastUsedFrame] : m_Textures)
        {
            if (lastUsedFrame == m_GlobalFrameNumber || RGTexture->Get()->GetDescription() != textureDesc)
            {
                ++handleID;
                continue;
            }

            lastUsedFrame = m_GlobalFrameNumber;

            auto& gfxTextureHandle = RGTexture->Get();
            if (gfxTextureHandle->Resize(textureDesc.Dimensions)) m_DeviceRMA.m_ResourcesNeededMemoryRebind.emplace(resourceID);

            SetTextureDebugNameFunc(textureName, *gfxTextureHandle);
            return handleID;
        }

        handleID        = m_Textures.size();
        auto& RGTexture = m_Textures.emplace_back(MakeUnique<RenderGraphResourceTexture>(MakeUnique<GfxTexture>(m_Device, textureDesc)),
                                                  m_GlobalFrameNumber);
        SetTextureDebugNameFunc(textureName, *(RGTexture.Handle->Get()));
        m_DeviceRMA.m_ResourcesNeededMemoryRebind.emplace(resourceID);
        return handleID;
    }

    NODISCARD RGBufferHandle RenderGraphResourcePool::CreateBuffer(const GfxBufferDescription& bufferDesc, const std::string& bufferName,
                                                                   const RGResourceID& resourceID) noexcept
    {
        const auto SetBufferDebugNameFunc = [&](const std::string& bufferName, const vk::Buffer& buffer)
        { m_Device->SetDebugName(bufferName, buffer); };

        const auto CreateBufferFunc = [&](GfxBufferVector& bufferVector,
                                          RenderGraphResourcePool::ResourceMemoryAliaser& rma) -> RGBufferHandle
        {
            RGBufferHandle handleID{0};
            for (auto& [RGBuffer, lastUsedFrame] : bufferVector)
            {
                if (lastUsedFrame == m_GlobalFrameNumber || RGBuffer->Get()->GetDescription() != bufferDesc)
                {
                    ++handleID.ID;
                    continue;
                }

                handleID.BufferFlags  = bufferDesc.ExtraFlags;
                lastUsedFrame         = m_GlobalFrameNumber;
                auto& gfxBufferHandle = RGBuffer->Get();

                if (gfxBufferHandle->Resize(bufferDesc.Capacity, bufferDesc.ElementSize))
                    rma.m_ResourcesNeededMemoryRebind.emplace(resourceID);

                SetBufferDebugNameFunc(bufferName, *gfxBufferHandle);
                return handleID;
            }

            handleID       = RenderGraphBufferHandle{.ID = bufferVector.size(), .BufferFlags = bufferDesc.ExtraFlags};
            auto& RGBuffer = bufferVector.emplace_back(MakeUnique<RenderGraphResourceBuffer>(MakeUnique<GfxBuffer>(m_Device, bufferDesc)),
                                                       m_GlobalFrameNumber);
            SetBufferDebugNameFunc(bufferName, *(RGBuffer.Handle->Get()));
            rma.m_ResourcesNeededMemoryRebind.emplace(resourceID);
            return handleID;
        };

        // NOTE: Handling rebar first cuz it contains device and host bits!
        if (bufferDesc.ExtraFlags == EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT)
            return CreateBufferFunc(m_ReBARBuffers[m_CurrentFrameIndex], m_ReBARRMA[m_CurrentFrameIndex]);

        if (bufferDesc.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT)
            return CreateBufferFunc(m_HostBuffers[m_CurrentFrameIndex], m_HostRMA[m_CurrentFrameIndex]);

        if (bufferDesc.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)
            return CreateBufferFunc(m_DeviceBuffers, m_DeviceRMA);

        RDNT_ASSERT(false, "{}: nothing to return!");
    }

    void RenderGraphResourcePool::ResourceMemoryAliaser::BindResourcesToMemoryRegions() noexcept
    {
        // TODO: Instead of checking if we even need clear, store set/map of resource properties(memory requirements/flags)
        // by render graph resource ID.
        // NOTE: Clear resources if they aren't being created anymore, but being stored.
        bool bNeedMemoryDefragmentation = false;
        {
            u64 resourcesInBuckets = 0;
            for (const auto& bucket : m_MemoryBuckets)
            {
                resourcesInBuckets += bucket.AlreadyAliasedResources.size();
                if (resourcesInBuckets > m_ResourceInfoMap.size())
                {
                    bNeedMemoryDefragmentation = true;
                    break;
                }

                for (const auto& aliasedResource : bucket.AlreadyAliasedResources)
                {
                    if (m_ResourceInfoMap.contains(aliasedResource.ID) &&
                        aliasedResource.MemoryPropertyFlags == m_ResourceInfoMap[aliasedResource.ID].MemoryPropertyFlags &&
                        aliasedResource.MemoryRequirements == m_ResourceInfoMap[aliasedResource.ID].MemoryRequirements)
                        continue;

                    bNeedMemoryDefragmentation = true;
                    break;
                }

                if (bNeedMemoryDefragmentation) break;
            }
            bNeedMemoryDefragmentation = bNeedMemoryDefragmentation || (resourcesInBuckets != m_ResourceInfoMap.size());
        }
        if (!bNeedMemoryDefragmentation && m_ResourcesNeededMemoryRebind.empty()) return;
        CleanMemoryBuckets();
        RDNT_ASSERT(!m_ResourceInfoMap.empty(), "Resource Info Map is invalid!");

        struct RenderGraphResourceUnaliased
        {
            RGResourceHandleVariant ResourceHandle{};
            RGResourceID ID{};
            std::string DebugName{s_DEFAULT_STRING};
            vk::MemoryRequirements MemoryRequirements{};
            vk::MemoryPropertyFlags MemoryPropertyFlags{};
        };

        // NOTE: Firstly invalidate resources cuz their allocation might be deleted, thne populate resources,
        // sort them in ascending order and start aliasing from the highest memory usage resource.
        std::vector<RenderGraphResourceUnaliased> unaliasedResources;
        for (const auto& [resourceID, resourceInfo] : m_ResourceInfoMap)
        {
            auto memoryRequirements = resourceInfo.MemoryRequirements;
            if (bNeedMemoryDefragmentation || !m_ResourcesNeededMemoryRebind.contains(resourceID))
            {
                if (auto* rgTextureHandle = std::get_if<RGTextureHandle>(&resourceInfo.ResourceHandle))
                {
                    auto& gfxTextureHandle = m_ResourcePoolPtr->GetTexture(*rgTextureHandle)->Get();
                    gfxTextureHandle->Invalidate();
                    memoryRequirements = GfxContext::Get().GetDevice()->GetLogicalDevice()->getImageMemoryRequirements(*gfxTextureHandle);
                }
                else if (auto* rgBufferHandle = std::get_if<RGBufferHandle>(&resourceInfo.ResourceHandle))
                {
                    auto& gfxBufferHandle = m_ResourcePoolPtr->GetBuffer(*rgBufferHandle)->Get();
                    gfxBufferHandle->Invalidate();
                    memoryRequirements = GfxContext::Get().GetDevice()->GetLogicalDevice()->getBufferMemoryRequirements(*gfxBufferHandle);
                }
            }

            unaliasedResources.emplace_back(resourceInfo.ResourceHandle, resourceID, resourceInfo.DebugName, memoryRequirements,
                                            resourceInfo.MemoryPropertyFlags);
        }
        std::sort(std::execution::par, unaliasedResources.begin(), unaliasedResources.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.MemoryRequirements.size < rhs.MemoryRequirements.size; });
        m_ResourcesNeededMemoryRebind.clear();

        while (!unaliasedResources.empty())
        {
            bool bResourceAssigned    = false;
            auto resourceToBeAssigned = std::move(unaliasedResources.back());
            unaliasedResources.pop_back();

            for (auto& memoryBucket : m_MemoryBuckets)
            {
                // NOTES:
                // 1) First row's resource in bucket fully occupies it!
                // 2) Memory type should be the same!
                if (DoEffectiveLifetimesIntersect(m_ResourceLifetimeMap[memoryBucket.AlreadyAliasedResources.front().ID],
                                                  m_ResourceLifetimeMap[resourceToBeAssigned.ID]) ||
                    resourceToBeAssigned.MemoryPropertyFlags != memoryBucket.AlreadyAliasedResources.front().MemoryPropertyFlags)
                    continue;

                enum class EMemoryOffsetType : u8
                {
                    MEMORY_OFFSET_TYPE_START,
                    MEMORY_OFFSET_TYPE_END
                };
                using MemoryOffset                                  = std::pair<uint64_t, EMemoryOffsetType>;
                std::vector<MemoryOffset> nonAliasableMemoryOffsets = {{0, EMemoryOffsetType::MEMORY_OFFSET_TYPE_END}};

                // Build nonaliasable memory offsets for every resource each time we wanna emplace new resource.
                for (const auto& aliasedResource : memoryBucket.AlreadyAliasedResources)
                {
                    if (DoEffectiveLifetimesIntersect(m_ResourceLifetimeMap[aliasedResource.ID],
                                                      m_ResourceLifetimeMap[resourceToBeAssigned.ID]))
                    {
                        const u64 byteOffsetStart = aliasedResource.Offset;
                        const u64 byteOffsetEnd   = byteOffsetStart + aliasedResource.MemoryRequirements.size;

                        nonAliasableMemoryOffsets.emplace_back(byteOffsetStart, EMemoryOffsetType::MEMORY_OFFSET_TYPE_START);
                        nonAliasableMemoryOffsets.emplace_back(byteOffsetEnd, EMemoryOffsetType::MEMORY_OFFSET_TYPE_END);
                    }
                }
                nonAliasableMemoryOffsets.emplace_back(memoryBucket.AlreadyAliasedResources.front().MemoryRequirements.size,
                                                       EMemoryOffsetType::MEMORY_OFFSET_TYPE_START);

                std::sort(std::execution::par, nonAliasableMemoryOffsets.begin(), nonAliasableMemoryOffsets.end(),
                          [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

                std::optional<std::pair</* OffsetBytes */ u64, /* SizeBytes */ u64>> foundMemoryRegion{std::nullopt};
                i64 overlapCounter{0};
                for (u64 i{}; i < nonAliasableMemoryOffsets.size() - 1; ++i)
                {
                    const auto& [currentOffset, currentType] = nonAliasableMemoryOffsets[i];
                    const auto& [nextOffset, nextType]       = nonAliasableMemoryOffsets[i + 1];
                    overlapCounter = std::max(overlapCounter + (currentType == EMemoryOffsetType::MEMORY_OFFSET_TYPE_START ? 1 : -1), 0ll);

                    const bool bReachedAliasableRegion = overlapCounter == 0 && currentType == EMemoryOffsetType::MEMORY_OFFSET_TYPE_END &&
                                                         nextType == EMemoryOffsetType::MEMORY_OFFSET_TYPE_START;

                    const u64 alignedOffset = Math::AlignUp(
                        currentOffset, resourceToBeAssigned.MemoryRequirements.alignment);  // vkBind*Memory requires aligned location
                    const u64 memoryRegionSize = nextOffset - alignedOffset;
                    const bool bRegionValid    = memoryRegionSize > 0;

                    const bool bCanFitInsideAllocation = alignedOffset + resourceToBeAssigned.MemoryRequirements.size <=
                                                         memoryBucket.AlreadyAliasedResources.front().MemoryRequirements.size;
                    if (!bRegionValid || !bReachedAliasableRegion || !bCanFitInsideAllocation) continue;

                    if (!foundMemoryRegion.has_value() ||
                        memoryRegionSize <= (*foundMemoryRegion).second && resourceToBeAssigned.MemoryRequirements.size < memoryRegionSize)
                        foundMemoryRegion = {alignedOffset, memoryRegionSize};
                }

                if (foundMemoryRegion.has_value())
                {
                    memoryBucket.AlreadyAliasedResources.emplace_back(
                        resourceToBeAssigned.ResourceHandle, resourceToBeAssigned.ID, (*foundMemoryRegion).first,
                        resourceToBeAssigned.DebugName, resourceToBeAssigned.MemoryRequirements, resourceToBeAssigned.MemoryPropertyFlags);
                    bResourceAssigned = true;

                    break;
                }
            }

            if (!bResourceAssigned)
            {
                auto& bucket = m_MemoryBuckets.emplace_back();
                bucket.AlreadyAliasedResources.emplace_back(resourceToBeAssigned.ResourceHandle, resourceToBeAssigned.ID, 0,
                                                            resourceToBeAssigned.DebugName, resourceToBeAssigned.MemoryRequirements,
                                                            resourceToBeAssigned.MemoryPropertyFlags);
            }
        }

        // 1. Gather memory requirements.
        // 2. Bind resource to memory.
        for (auto& memoryBucket : m_MemoryBuckets)
        {
            RDNT_ASSERT(!memoryBucket.AlreadyAliasedResources.empty(), "MemoryBucket is invalid!");
            // NOTE: First row's resource in bucket fully occupies it!
            memoryBucket.MemoryRequirements  = memoryBucket.AlreadyAliasedResources.front().MemoryRequirements;
            memoryBucket.MemoryPropertyFlags = memoryBucket.AlreadyAliasedResources.front().MemoryPropertyFlags;

            for (const auto& aliasedResource : memoryBucket.AlreadyAliasedResources)
            {
                memoryBucket.MemoryRequirements.alignment =
                    std::max(memoryBucket.MemoryRequirements.alignment, aliasedResource.MemoryRequirements.alignment);
                memoryBucket.MemoryRequirements.memoryTypeBits &= aliasedResource.MemoryRequirements.memoryTypeBits;
                memoryBucket.MemoryPropertyFlags |= aliasedResource.MemoryPropertyFlags;
            }
            RDNT_ASSERT(memoryBucket.MemoryRequirements.memoryTypeBits != 0,
                        "Invalid memory type bits! Failed to determine memoryType for memory bucket!");

            m_ResourcePoolPtr->m_Device->AllocateMemory(memoryBucket.Allocation, memoryBucket.MemoryRequirements,
                                                        memoryBucket.MemoryPropertyFlags);
            for (const auto& aliasedResource : memoryBucket.AlreadyAliasedResources)
            {
                if (auto* rgTextureHandle = std::get_if<RGTextureHandle>(&aliasedResource.ResourceHandle))
                {
                    auto& gfxTextureHandle = m_ResourcePoolPtr->GetTexture(*rgTextureHandle)->Get();
                    m_ResourcePoolPtr->m_Device->BindTexture(*gfxTextureHandle, memoryBucket.Allocation, aliasedResource.Offset);
                    gfxTextureHandle->RG_Finalize();
                }

                if (auto* rgBufferHandle = std::get_if<RGBufferHandle>(&aliasedResource.ResourceHandle))
                {
                    auto& gfxBufferHandle = m_ResourcePoolPtr->GetBuffer(*rgBufferHandle)->Get();
                    m_ResourcePoolPtr->m_Device->BindBuffer(*gfxBufferHandle, memoryBucket.Allocation, aliasedResource.Offset);
                    gfxBufferHandle->RG_Finalize(memoryBucket.Allocation);
                }
            }
        }
    }

}  // namespace Radiant
