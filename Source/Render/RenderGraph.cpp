#include <pch.h>
#include "RenderGraph.hpp"

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

        constexpr auto IsAccessFlagPresentFunc = [](const vk::AccessFlags2& accessMask, const vk::AccessFlagBits2& accessFlag)
        { return (accessMask & accessFlag) == accessFlag; };

        NODISCARD static void FillBufferBarrierIfNeeded(UnorderedSet<vk::MemoryBarrier2>& memoryBarriers,
                                                        std::vector<vk::BufferMemoryBarrier2>& bufferMemoryBarriers,
                                                        const Unique<GfxBuffer>& buffer, const ResourceStateFlags currentState,
                                                        const ResourceStateFlags nextState) noexcept
        {
            // NOTE: BufferMemoryBarriers should be used only on queue ownership transfers.
            // auto bufferMemoryBarrier =
            // vk::BufferMemoryBarrier2().setBuffer(*buffer).setOffset(0).setSize(buffer->GetDescription().Capacity);

            vk::PipelineStageFlags2 srcStageMask{vk::PipelineStageFlagBits2::eNone};
            vk::AccessFlags2 srcAccessMask{vk::AccessFlagBits2::eNone};
            vk::PipelineStageFlags2 dstStageMask{vk::PipelineStageFlagBits2::eNone};
            vk::AccessFlags2 dstAccessMask{vk::AccessFlagBits2::eNone};

            if (currentState == EResourceState::RESOURCE_STATE_UNDEFINED) srcStageMask |= vk::PipelineStageFlagBits2::eBottomOfPipe;

            const bool bCurrentStateShaderResource =
                (currentState &
                 (EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE | EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE |
                  EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE)) ==
                (EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE | EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE |
                 EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);

            if (bCurrentStateShaderResource && (currentState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
            {
                srcAccessMask |=
                    vk::AccessFlagBits2::eShaderRead;  // NOTE: This access implies both eShaderStorageRead & eShaderSampledRead
            }
            if (bCurrentStateShaderResource &&
                (currentState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
            {
                srcAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            const bool bNextStateShaderResource =
                (nextState &
                 (EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE | EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE |
                  EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE)) ==
                (EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE | EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE |
                 EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);

            if (bNextStateShaderResource && (nextState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
            {
                dstAccessMask |=
                    vk::AccessFlagBits2::eShaderRead;  // NOTE: This access implies both eShaderStorageRead & eShaderSampledRead
            }
            if (bNextStateShaderResource && (nextState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
            {
                dstAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            // CURRENT STATE
            if ((currentState & EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE)
            {
                srcStageMask |= vk::PipelineStageFlagBits2::eComputeShader;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE)
            {
                srcStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_COPY_SOURCE) == EResourceState::RESOURCE_STATE_COPY_SOURCE)
            {
                // NOTE: Src copy buffer likes eTransferRead, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                srcAccessMask |= vk::AccessFlagBits2::eTransferRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_COPY_DESTINATION) == EResourceState::RESOURCE_STATE_COPY_DESTINATION)
            {
                // NOTE: Dst copy buffer likes eTransferWrite, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderWrite;
                srcAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_INDEX_BUFFER) == EResourceState::RESOURCE_STATE_INDEX_BUFFER)
            {
                srcAccessMask |= vk::AccessFlagBits2::eIndexRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eIndexInput;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_VERTEX_BUFFER) == EResourceState::RESOURCE_STATE_VERTEX_BUFFER ||
                (currentState & EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE) ==
                    EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE)
            {
                srcAccessMask |= vk::AccessFlagBits2::eMemoryRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_UNIFORM_BUFFER) == EResourceState::RESOURCE_STATE_UNIFORM_BUFFER)
            {
                // NOTE: Uniform buffer likes eUniformRead, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                srcAccessMask |= vk::AccessFlagBits2::eUniformRead;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_INDIRECT_ARGUMENT) == EResourceState::RESOURCE_STATE_INDIRECT_ARGUMENT)
            {
                // NOTE: Indirect arg buffer likes eIndirectCommandRead, but not eShaderStorageRead & eShaderSampledRead.
                srcAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                srcAccessMask |= vk::AccessFlagBits2::eIndirectCommandRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eDrawIndirect;
            }

            if ((currentState & (EResourceState::RESOURCE_STATE_STORAGE_BUFFER | EResourceState::RESOURCE_STATE_READ)) ==
                (EResourceState::RESOURCE_STATE_STORAGE_BUFFER | EResourceState::RESOURCE_STATE_READ))
            {
                srcAccessMask |= vk::AccessFlagBits2::eShaderRead;
            }

            if ((currentState & (EResourceState::RESOURCE_STATE_STORAGE_BUFFER | EResourceState::RESOURCE_STATE_WRITE)) ==
                (EResourceState::RESOURCE_STATE_STORAGE_BUFFER | EResourceState::RESOURCE_STATE_WRITE))
            {
                srcAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            // NEXT STATE
            if ((nextState & EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE)
            {
                dstStageMask |= vk::PipelineStageFlagBits2::eComputeShader;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_INDEX_BUFFER) == EResourceState::RESOURCE_STATE_INDEX_BUFFER)
            {
                dstAccessMask |= vk::AccessFlagBits2::eIndexRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eIndexInput;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_VERTEX_BUFFER) == EResourceState::RESOURCE_STATE_VERTEX_BUFFER ||
                (nextState & EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE) ==
                    EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE)
            {
                dstAccessMask |= vk::AccessFlagBits2::eMemoryRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_COPY_SOURCE) == EResourceState::RESOURCE_STATE_COPY_SOURCE)
            {
                // NOTE: Src copy buffer likes eTransferRead, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                dstAccessMask |= vk::AccessFlagBits2::eTransferRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_COPY_DESTINATION) == EResourceState::RESOURCE_STATE_COPY_DESTINATION)
            {
                // NOTE: Dst copy buffer likes eTransferWrite, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderWrite;
                dstAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_UNIFORM_BUFFER) == EResourceState::RESOURCE_STATE_UNIFORM_BUFFER)
            {
                // NOTE: Uniform buffer likes eUniformRead, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                dstAccessMask |= vk::AccessFlagBits2::eUniformRead;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_INDIRECT_ARGUMENT) == EResourceState::RESOURCE_STATE_INDIRECT_ARGUMENT)
            {
                // NOTE: Indirect arg buffer likes eIndirectCommandRead, but not eShaderStorageRead & eShaderSampledRead.
                dstAccessMask ^= vk::AccessFlagBits2::eShaderRead;
                dstAccessMask |= vk::AccessFlagBits2::eIndirectCommandRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eDrawIndirect;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE)
            {
                dstStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            constexpr auto ssboReadState = EResourceState::RESOURCE_STATE_STORAGE_BUFFER | EResourceState::RESOURCE_STATE_READ;
            if ((nextState & ssboReadState) == ssboReadState)
            {
                dstAccessMask |= vk::AccessFlagBits2::eShaderRead;
            }

            constexpr auto ssboWriteState = EResourceState::RESOURCE_STATE_STORAGE_BUFFER | EResourceState::RESOURCE_STATE_WRITE;
            if ((nextState & ssboWriteState) == ssboWriteState)
            {
                dstAccessMask |= vk::AccessFlagBits2::eShaderWrite;
            }

            // NOTE: Read-To-Read don't need any sync.
            const bool bIsAnyWriteOpPresent =
                /* srcAccessMask */ IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eShaderWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eTransferWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eHostWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eMemoryWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eShaderStorageWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                /* dstAccessMask */ IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eShaderWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eTransferWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eHostWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eMemoryWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eShaderStorageWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR);

            if (bIsAnyWriteOpPresent)
                memoryBarriers.emplace(vk::MemoryBarrier2()
                                           .setSrcAccessMask(srcAccessMask)
                                           .setSrcStageMask(srcStageMask)
                                           .setDstAccessMask(dstAccessMask)
                                           .setDstStageMask(dstStageMask));
        }

        NODISCARD static void FillImageBarrierIfNeeded(UnorderedSet<vk::MemoryBarrier2>& memoryBarriers,
                                                       std::vector<vk::ImageMemoryBarrier2>& imageMemoryBarriers,
                                                       const Unique<GfxTexture>& texture, const ResourceStateFlags currentState,
                                                       const ResourceStateFlags nextState, vk::ImageLayout& outNextLayout) noexcept
        {
            constexpr auto bestDepthStencilState = EResourceState::RESOURCE_STATE_DEPTH_READ | EResourceState::RESOURCE_STATE_DEPTH_WRITE;

            vk::AccessFlags2 srcAccessMask{vk::AccessFlagBits2::eNone};
            vk::PipelineStageFlags2 srcStageMask{vk::PipelineStageFlagBits2::eNone};
            vk::ImageLayout oldLayout{vk::ImageLayout::eUndefined};

            vk::AccessFlags2 dstAccessMask{vk::AccessFlagBits2::eNone};
            vk::PipelineStageFlags2 dstStageMask{vk::PipelineStageFlagBits2::eNone};

            if (currentState == EResourceState::RESOURCE_STATE_UNDEFINED) srcStageMask |= vk::PipelineStageFlagBits2::eBottomOfPipe;

            // CURRENT STATE
            if ((currentState & EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE)
            {
                // NOTE: Tbh idk which way I should determine layout here but, my logic is that if you write to it, then it's eGeneral, but
                // if you only read it's eShaderReadOnlyOptimal, simple as that.
                if ((currentState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
                {
                    oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                    // srcAccessMask |= vk::AccessFlagBits2::eShaderStorageRead;
                }

                if ((currentState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
                {
                    oldLayout = vk::ImageLayout::eGeneral;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
                }

                srcStageMask |= vk::PipelineStageFlagBits2::eComputeShader;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE)
            {
                if ((currentState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
                {
                    oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                }

                if ((currentState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
                {
                    oldLayout = vk::ImageLayout::eGeneral;
                    srcAccessMask |= vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
                }

                srcStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE)
            {
                oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_RENDER_TARGET) == EResourceState::RESOURCE_STATE_RENDER_TARGET)
            {
                oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
                if ((currentState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
                {
                    srcAccessMask |= vk::AccessFlagBits2::eColorAttachmentRead;
                }

                if ((currentState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
                {
                    srcAccessMask |= vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead;
                }

                srcStageMask |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_DEPTH_READ) == EResourceState::RESOURCE_STATE_DEPTH_READ)
            {
                oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_DEPTH_WRITE) == EResourceState::RESOURCE_STATE_DEPTH_WRITE)
            {
                oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_COPY_SOURCE) == EResourceState::RESOURCE_STATE_COPY_SOURCE)
            {
                oldLayout = vk::ImageLayout::eTransferSrcOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eTransferRead;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if ((currentState & EResourceState::RESOURCE_STATE_COPY_DESTINATION) == EResourceState::RESOURCE_STATE_COPY_DESTINATION)
            {
                oldLayout = vk::ImageLayout::eTransferDstOptimal;
                srcAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                srcStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            // NEXT STATE
            if ((nextState & EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE)
            {
                if ((nextState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
                {
                    if ((currentState & EResourceState::RESOURCE_STATE_RENDER_TARGET) == EResourceState::RESOURCE_STATE_RENDER_TARGET ||
                        (currentState & EResourceState::RESOURCE_STATE_DEPTH_READ) == EResourceState::RESOURCE_STATE_DEPTH_READ ||
                        (currentState & EResourceState::RESOURCE_STATE_DEPTH_WRITE) == EResourceState::RESOURCE_STATE_DEPTH_WRITE)
                    {
                        outNextLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                        dstAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                    }

                    //   dstAccessMask |= vk::AccessFlagBits2::eShaderStorageRead;
                }

                if ((nextState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
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

            if ((nextState & EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE)
            {
                if ((nextState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
                {
                    outNextLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    dstAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                }

                if ((nextState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
                {
                    outNextLayout = vk::ImageLayout::eGeneral;
                    dstAccessMask |= vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
                }

                dstStageMask |= vk::PipelineStageFlagBits2::eFragmentShader;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_DEPTH_READ) == EResourceState::RESOURCE_STATE_DEPTH_READ)
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

            if ((nextState & EResourceState::RESOURCE_STATE_DEPTH_WRITE) == EResourceState::RESOURCE_STATE_DEPTH_WRITE)
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

            if ((nextState & EResourceState::RESOURCE_STATE_RENDER_TARGET) == EResourceState::RESOURCE_STATE_RENDER_TARGET)
            {
                outNextLayout = vk::ImageLayout::eColorAttachmentOptimal;
                if ((nextState & EResourceState::RESOURCE_STATE_READ) == EResourceState::RESOURCE_STATE_READ)
                {
                    dstAccessMask |= vk::AccessFlagBits2::eColorAttachmentRead;
                }

                if ((nextState & EResourceState::RESOURCE_STATE_WRITE) == EResourceState::RESOURCE_STATE_WRITE)
                {
                    dstAccessMask |= vk::AccessFlagBits2::eColorAttachmentWrite;
                }

                dstStageMask |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE) ==
                EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE)
            {
                outNextLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eShaderSampledRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eVertexShader;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_COPY_SOURCE) == EResourceState::RESOURCE_STATE_COPY_SOURCE)
            {
                outNextLayout = vk::ImageLayout::eTransferSrcOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eTransferRead;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if ((nextState & EResourceState::RESOURCE_STATE_COPY_DESTINATION) == EResourceState::RESOURCE_STATE_COPY_DESTINATION)
            {
                outNextLayout = vk::ImageLayout::eTransferDstOptimal;
                dstAccessMask |= vk::AccessFlagBits2::eTransferWrite;
                dstStageMask |= vk::PipelineStageFlagBits2::eAllTransfer;
            }

            if (oldLayout == outNextLayout && oldLayout == vk::ImageLayout::eUndefined || outNextLayout == vk::ImageLayout::eUndefined)
                RDNT_ASSERT(false, "Failed to determine image barrier!");

            // NOTE: Read-To-Read don't need any sync.
            const bool bIsAnyWriteOpPresent =
                /* srcAccessMask */ IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eShaderWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eTransferWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eHostWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eMemoryWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eShaderStorageWrite) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                IsAccessFlagPresentFunc(srcAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                /* dstAccessMask */ IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eShaderWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eTransferWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eHostWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eMemoryWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eShaderStorageWrite) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR) ||
                IsAccessFlagPresentFunc(dstAccessMask, vk::AccessFlagBits2::eAccelerationStructureWriteKHR);

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
                auto imageMemoryBarrier = vk::ImageMemoryBarrier2()
                                              .setImage(*texture)
                                              .setSrcAccessMask(srcAccessMask)
                                              .setSrcStageMask(srcStageMask)
                                              .setOldLayout(oldLayout)
                                              .setDstAccessMask(dstAccessMask)
                                              .setDstStageMask(dstStageMask)
                                              .setNewLayout(outNextLayout);
                if (texture->IsDepthFormat(texture->GetDescription().Format))
                {
                    imageMemoryBarrier.setSubresourceRange(vk::ImageSubresourceRange()
                                                               .setBaseArrayLayer(0)
                                                               .setLayerCount(1)
                                                               .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                                                               .setBaseMipLevel(0)
                                                               .setLevelCount(1));
                }
                else
                {
                    imageMemoryBarrier.setSubresourceRange(vk::ImageSubresourceRange()
                                                               .setBaseArrayLayer(0)
                                                               .setLayerCount(1)
                                                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                               .setBaseMipLevel(0)
                                                               .setLevelCount(1));
                }
                imageMemoryBarriers.emplace_back(imageMemoryBarrier);
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
                for (const auto& outTexture : writePass->m_TextureWrites)
                {
                    bAnyDependencyFound = std::find(readPass->m_TextureReads.cbegin(), readPass->m_TextureReads.cend(), outTexture) !=
                                          readPass->m_TextureReads.cend();

                    if (bAnyDependencyFound) break;
                }
                if (bAnyDependencyFound)
                {
                    m_AdjacencyLists[writePass->m_ID].emplace_back(readPass->m_ID);
                    continue;
                }

                for (const auto& outBuffer : writePass->m_BufferWrites)
                {
                    bAnyDependencyFound = std::find(readPass->m_BufferReads.cbegin(), readPass->m_BufferReads.cend(), outBuffer) !=
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
                if ((bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) ==
                    EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL)
                    memoryPropertyFlags |= vk::MemoryPropertyFlagBits::eDeviceLocal;

                if ((bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST)
                    memoryPropertyFlags |= vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                if ((bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR) ==
                    EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR)
                    memoryPropertyFlags = vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible |
                                          vk::MemoryPropertyFlagBits::eHostCoherent;

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

        frameData.GeneralCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_GfxContext->GetBindlessPipelineLayout(), 0,
                                                          frameData.DescriptorSet, {});
        frameData.GeneralCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_GfxContext->GetBindlessPipelineLayout(), 0,
                                                          frameData.DescriptorSet, {});

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
        // Sort passes by type to not drain the pipeline fully.
        std::sort(std::execution::par, m_Passes.begin(), m_Passes.end(),
                  [](const auto* lhs, const auto* rhs) { return lhs->m_PassType < rhs->m_PassType; });

        auto& frameData = gfxContext->GetCurrentFrameData();

        auto& cmd = frameData.GeneralCommandBuffer;
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

            std::vector<vk::ImageMemoryBarrier2> imageMemoryBarriers;
            std::vector<vk::BufferMemoryBarrier2> bufferMemoryBarriers;
            UnorderedSet<vk::MemoryBarrier2> memoryBarriers;

            // TODO: Fill stencil
            auto stencilAttachmentInfo = vk::RenderingAttachmentInfo();
            auto depthAttachmentInfo   = vk::RenderingAttachmentInfo();
            std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos;
            u32 layerCount{1};

            if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
                RDNT_ASSERT(currentPass->m_Viewport.has_value(), "Viewport is invalid!");

            for (const auto& resourceID : currentPass->m_BufferReads)
            {
                auto& RGbuffer = m_RenderGraph.m_ResourcePool->GetBuffer(m_RenderGraph.m_ResourceIDToBufferHandle[resourceID]);
                auto& buffer   = RGbuffer->Get();

                const auto currentState = RGbuffer->GetState();
                const auto nextState    = currentPass->m_ResourceIDToResourceState[resourceID];

                RenderGraphUtils::FillBufferBarrierIfNeeded(memoryBarriers, bufferMemoryBarriers, buffer, currentState, nextState);
                RGbuffer->SetState(nextState);
            }

            for (const auto& resourceID : currentPass->m_BufferWrites)
            {
                auto& RGbuffer = m_RenderGraph.m_ResourcePool->GetBuffer(m_RenderGraph.m_ResourceIDToBufferHandle[resourceID]);
                auto& buffer   = RGbuffer->Get();

                const auto currentState = RGbuffer->GetState();
                const auto nextState    = currentPass->m_ResourceIDToResourceState[resourceID];

                RenderGraphUtils::FillBufferBarrierIfNeeded(memoryBarriers, bufferMemoryBarriers, buffer, currentState, nextState);
                RGbuffer->SetState(nextState);
            }

            for (const auto& resourceID : currentPass->m_TextureReads)
            {
                auto& RGtexture = m_RenderGraph.m_ResourcePool->GetTexture(m_RenderGraph.m_ResourceIDToTextureHandle[resourceID]);
                auto& texture   = RGtexture->Get();

                const auto currentState = RGtexture->GetState();
                const auto nextState    = currentPass->m_ResourceIDToResourceState[resourceID];

                vk::ImageLayout nextLayout{vk::ImageLayout::eUndefined};
                RenderGraphUtils::FillImageBarrierIfNeeded(memoryBarriers, imageMemoryBarriers, texture, currentState, nextState,
                                                           nextLayout);
                RGtexture->SetState(nextState);

                if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
                {
                    // NOTE: Since vulkan allows writing to storage texture from fragment shader we should take that into account
                    // NOTE: In case we use attachment as read only, other not supported!
                    const bool bIsRasterUsage =
                        (nextState & EResourceState::RESOURCE_STATE_RENDER_TARGET) == EResourceState::RESOURCE_STATE_RENDER_TARGET ||
                        (nextState & EResourceState::RESOURCE_STATE_DEPTH_READ) == EResourceState::RESOURCE_STATE_DEPTH_READ ||
                        (nextState & EResourceState::RESOURCE_STATE_DEPTH_WRITE) == EResourceState::RESOURCE_STATE_DEPTH_WRITE;
                    if (!bIsRasterUsage) continue;

                    layerCount = std::max(layerCount, texture->GetDescription().LayerCount);
                    if (texture->IsDepthFormat(texture->GetDescription().Format))
                    {
                        depthAttachmentInfo = texture->GetRenderingAttachmentInfo(nextLayout, {}, vk::AttachmentLoadOp::eLoad,
                                                                                  vk::AttachmentStoreOp::eDontCare);
                    }
                    else
                    {
                        colorAttachmentInfos.emplace_back() = texture->GetRenderingAttachmentInfo(
                            nextLayout, {}, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eDontCare);
                    }
                }
            }

            for (const auto& resourceID : currentPass->m_TextureWrites)
            {
                auto& RGtexture = m_RenderGraph.m_ResourcePool->GetTexture(m_RenderGraph.m_ResourceIDToTextureHandle[resourceID]);
                auto& texture   = RGtexture->Get();

                const auto currentState = RGtexture->GetState();
                const auto nextState    = currentPass->m_ResourceIDToResourceState[resourceID];
                vk::ImageLayout nextLayout{vk::ImageLayout::eUndefined};
                RenderGraphUtils::FillImageBarrierIfNeeded(memoryBarriers, imageMemoryBarriers, texture, currentState, nextState,
                                                           nextLayout);
                RGtexture->SetState(nextState);

                if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
                {
                    // NOTE: Since vulkan allows writing to storage texture from fragment shader we should take that into account
                    const bool bIsRasterUsage =
                        (nextState & EResourceState::RESOURCE_STATE_RENDER_TARGET) == EResourceState::RESOURCE_STATE_RENDER_TARGET ||
                        (nextState & EResourceState::RESOURCE_STATE_DEPTH_READ) == EResourceState::RESOURCE_STATE_DEPTH_READ ||
                        (nextState & EResourceState::RESOURCE_STATE_DEPTH_WRITE) == EResourceState::RESOURCE_STATE_DEPTH_WRITE;
                    if (!bIsRasterUsage) continue;

                    layerCount = std::max(layerCount, texture->GetDescription().LayerCount);
                    if (texture->IsDepthFormat(texture->GetDescription().Format))
                    {
                        depthAttachmentInfo = texture->GetRenderingAttachmentInfo(
                            nextLayout, vk::ClearValue().setDepthStencil(*currentPass->m_DepthStencilInfo->ClearValue),
                            currentPass->m_DepthStencilInfo->DepthLoadOp, currentPass->m_DepthStencilInfo->DepthStoreOp);
                    }
                    else
                    {
                        auto& currentRTInfo                 = currentPass->m_RenderTargetInfos[colorAttachmentInfos.size()];
                        colorAttachmentInfos.emplace_back() = texture->GetRenderingAttachmentInfo(
                            nextLayout, vk::ClearValue().setColor(*currentRTInfo.ClearValue), currentRTInfo.LoadOp, currentRTInfo.StoreOp);
                    }
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

            switch (currentPass->m_PassType)
            {
                case ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS:
                {
                    if (currentPass->m_DepthStencilInfo.has_value() || !currentPass->m_RenderTargetInfos.empty()) cmd.endRendering();

                    break;
                }
            }

            cpuTask.EndTime = Timer::GetElapsedSecondsFromNow(frameData.FrameStartTime);
            cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *frameData.TimestampsQueryPool,
                                frameData.CurrentTimestampIndex++);

#if RDNT_DEBUG
            cmd.endDebugUtilsLabelEXT();
#endif
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
        const auto resourceID                          = m_RenderGraph.CreateResourceID(name);
        m_RenderGraph.m_BufferCreates[name]            = bufferDesc;
        m_Pass.m_ResourceIDToResourceState[resourceID] = EResourceState::RESOURCE_STATE_UNDEFINED;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::ReadBuffer(const std::string& name,
                                                                    const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);
        m_Pass.m_BufferReads.emplace_back(resourceID);
        m_Pass.m_ResourceIDToResourceState[resourceID] |= resourceState | EResourceState::RESOURCE_STATE_READ;
        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::WriteBuffer(const std::string& name,
                                                                     const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);
        m_Pass.m_BufferWrites.emplace_back(resourceID);
        m_Pass.m_ResourceIDToResourceState[resourceID] |= resourceState | EResourceState::RESOURCE_STATE_WRITE;
        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    NODISCARD void RenderGraphResourceScheduler::WriteDepthStencil(const std::string& name, const vk::AttachmentLoadOp depthLoadOp,
                                                                   const vk::AttachmentStoreOp depthStoreOp,
                                                                   const vk::ClearDepthStencilValue& clearValue,
                                                                   const vk::AttachmentLoadOp stencilLoadOp,
                                                                   const vk::AttachmentStoreOp stencilStoreOp) noexcept
    {
        const auto resourceID = WriteTexture(name, EResourceState::RESOURCE_STATE_DEPTH_READ | EResourceState::RESOURCE_STATE_DEPTH_WRITE);
        m_Pass.m_DepthStencilInfo = {.ClearValue     = clearValue,
                                     .DepthLoadOp    = depthLoadOp,
                                     .DepthStoreOp   = depthStoreOp,
                                     .StencilLoadOp  = stencilLoadOp,
                                     .StencilStoreOp = stencilStoreOp};

        (void)resourceID;
    }

    NODISCARD void RenderGraphResourceScheduler::WriteRenderTarget(const std::string& name, const vk::AttachmentLoadOp loadOp,
                                                                   const vk::AttachmentStoreOp storeOp,
                                                                   const vk::ClearColorValue& clearValue) noexcept
    {
        const auto resourceID = WriteTexture(name, EResourceState::RESOURCE_STATE_RENDER_TARGET);
        m_Pass.m_RenderTargetInfos.emplace_back(clearValue, loadOp, storeOp);

        (void)resourceID;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::ReadTexture(const std::string& name,
                                                                     const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);
        m_Pass.m_TextureReads.emplace_back(resourceID);
        m_Pass.m_ResourceIDToResourceState[resourceID] |= resourceState | EResourceState::RESOURCE_STATE_READ;
        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    NODISCARD RGResourceID RenderGraphResourceScheduler::WriteTexture(const std::string& name,
                                                                      const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);
        m_Pass.m_TextureWrites.emplace_back(resourceID);
        m_Pass.m_ResourceIDToResourceState[resourceID] |=
            resourceState | EResourceState::RESOURCE_STATE_WRITE | EResourceState::RESOURCE_STATE_READ;
        m_RenderGraph.m_ResourcesUsedByPassesID[resourceID].emplace(m_Pass.m_ID);
        return resourceID;
    }

    void RenderGraphResourceScheduler::CreateTexture(const std::string& name, const GfxTextureDescription& textureDesc) noexcept
    {
        const auto resourceID                          = m_RenderGraph.CreateResourceID(name);
        m_RenderGraph.m_TextureCreates[name]           = textureDesc;
        m_Pass.m_ResourceIDToResourceState[resourceID] = EResourceState::RESOURCE_STATE_UNDEFINED;
    }

    void RenderGraphResourceScheduler::SetViewportScissors(const vk::Viewport& viewport, const vk::Rect2D& scissor) noexcept
    {
        m_Pass.m_Viewport = viewport;
        m_Pass.m_Scissor  = scissor;
    }

    NODISCARD RGTextureHandle RenderGraphResourcePool::CreateTexture(const GfxTextureDescription& textureDesc,
                                                                     const std::string& textureName,
                                                                     const RGResourceID& resourceID) noexcept
    {
        const auto setTextureDebugNameFunc = [&](const std::string& textureName, const vk::Image& image)
        { m_Device->SetDebugName(textureName, image); };

        RGTextureHandle handleID{0};
        for (auto& [RGTexture, lastUsedFrame] : m_Textures)
        {
            if (lastUsedFrame == m_GlobalFrameNumber && RGTexture->Get()->GetDescription() != textureDesc)
            {
                ++handleID;
                continue;
            }

            lastUsedFrame = m_GlobalFrameNumber;

            auto& gfxTextureHandle = RGTexture->Get();
            if (gfxTextureHandle->Resize(textureDesc.Dimensions)) m_DeviceRMA.m_ResourcesNeededMemoryRebind.emplace(resourceID);

            setTextureDebugNameFunc(textureName, *gfxTextureHandle);
            return handleID;
        }

        handleID        = m_Textures.size();
        auto& RGTexture = m_Textures.emplace_back(MakeUnique<RenderGraphResourceTexture>(MakeUnique<GfxTexture>(m_Device, textureDesc)),
                                                  m_GlobalFrameNumber);
        setTextureDebugNameFunc(textureName, *(RGTexture.Handle->Get()));
        m_DeviceRMA.m_ResourcesNeededMemoryRebind.emplace(resourceID);
        return handleID;
    }

    NODISCARD RGBufferHandle RenderGraphResourcePool::CreateBuffer(const GfxBufferDescription& bufferDesc, const std::string& bufferName,
                                                                   const RGResourceID& resourceID) noexcept
    {
        const auto setBufferDebugNameFunc = [&](const std::string& bufferName, const vk::Buffer& buffer)
        { m_Device->SetDebugName(bufferName, buffer); };

        RGBufferHandle handleID = {};

        // NOTE: Handling rebar first cuz it contains device and host bits!
        const bool bIsReBARPresent = (bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR) ==
                                     EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR;
        if (bIsReBARPresent)
        {
            for (auto& [RGBuffer, lastUsedFrame] : m_ReBARBuffers[m_CurrentFrameIndex])
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
                    m_ReBARRMA[m_CurrentFrameIndex].m_ResourcesNeededMemoryRebind.emplace(resourceID);

                setBufferDebugNameFunc(bufferName, *gfxBufferHandle);
                return handleID;
            }

            handleID = RenderGraphBufferHandle{.ID = m_ReBARBuffers[m_CurrentFrameIndex].size(), .BufferFlags = bufferDesc.ExtraFlags};
            auto& RGBuffer = m_ReBARBuffers[m_CurrentFrameIndex].emplace_back(
                MakeUnique<RenderGraphResourceBuffer>(MakeUnique<GfxBuffer>(m_Device, bufferDesc)), m_GlobalFrameNumber);
            setBufferDebugNameFunc(bufferName, *(RGBuffer.Handle->Get()));
            m_ReBARRMA[m_CurrentFrameIndex].m_ResourcesNeededMemoryRebind.emplace(resourceID);
            return handleID;
        }

        const bool bIsHostVisible =
            (bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST;

        handleID = {};
        if (bIsHostVisible)
        {
            for (auto& [RGBuffer, lastUsedFrame] : m_HostBuffers[m_CurrentFrameIndex])
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
                    m_HostRMA[m_CurrentFrameIndex].m_ResourcesNeededMemoryRebind.emplace(resourceID);

                setBufferDebugNameFunc(bufferName, *gfxBufferHandle);
                return handleID;
            }

            handleID       = RenderGraphBufferHandle{.ID = m_HostBuffers[m_CurrentFrameIndex].size(), .BufferFlags = bufferDesc.ExtraFlags};
            auto& RGBuffer = m_HostBuffers[m_CurrentFrameIndex].emplace_back(
                MakeUnique<RenderGraphResourceBuffer>(MakeUnique<GfxBuffer>(m_Device, bufferDesc)), m_GlobalFrameNumber);
            setBufferDebugNameFunc(bufferName, *(RGBuffer.Handle->Get()));
            m_HostRMA[m_CurrentFrameIndex].m_ResourcesNeededMemoryRebind.emplace(resourceID);
            return handleID;
        }

        const bool bIsDeviceLocal =
            (bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL;

        handleID = {};
        if (bIsDeviceLocal)
        {
            for (auto& [RGBuffer, lastUsedFrame] : m_DeviceBuffers)
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
                    m_DeviceRMA.m_ResourcesNeededMemoryRebind.emplace(resourceID);

                setBufferDebugNameFunc(bufferName, *gfxBufferHandle);
                return handleID;
            }

            handleID       = RenderGraphBufferHandle{.ID = m_DeviceBuffers.size(), .BufferFlags = bufferDesc.ExtraFlags};
            auto& RGBuffer = m_DeviceBuffers.emplace_back(
                MakeUnique<RenderGraphResourceBuffer>(MakeUnique<GfxBuffer>(m_Device, bufferDesc)), m_GlobalFrameNumber);
            setBufferDebugNameFunc(bufferName, *(RGBuffer.Handle->Get()));
            m_DeviceRMA.m_ResourcesNeededMemoryRebind.emplace(resourceID);
            return handleID;
        }

        RDNT_ASSERT(false, "{}: nothing to return!");
    }

    void RenderGraphResourcePool::BindResourcesToMemoryRegions() noexcept
    {
        m_DeviceRMA.BindResourcesToMemoryRegions();
        m_ReBARRMA[m_CurrentFrameIndex].BindResourcesToMemoryRegions();
        m_HostRMA[m_CurrentFrameIndex].BindResourcesToMemoryRegions();
    }

    void RenderGraphResourcePool::ResourceMemoryAliaser::BindResourcesToMemoryRegions() noexcept
    {
        if (m_ResourceInfoMap.empty() || m_ResourcesNeededMemoryRebind.empty()) return;
        CleanMemoryBuckets();

        struct RenderGraphResourceUnaliased
        {
            std::variant<RGTextureHandle, RGBufferHandle> ResourceHandle{};
            RGResourceID ID{};
            std::string DebugName{s_DEFAULT_STRING};
            vk::MemoryRequirements MemoryRequirements{};
            vk::MemoryPropertyFlags MemoryPropertyFlags{};
        };

        std::deque<RenderGraphResourceUnaliased> unaliasedResources;
        for (const auto& [resourceID, resourceInfo] : m_ResourceInfoMap)
        {
            unaliasedResources.emplace_back(resourceInfo.ResourceHandle, resourceID, resourceInfo.DebugName,
                                            resourceInfo.MemoryRequirements, resourceInfo.MemoryPropertyFlags);
        }
        std::sort(std::execution::par, unaliasedResources.begin(), unaliasedResources.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.MemoryRequirements.size > rhs.MemoryRequirements.size; });

        u64 memoryUsage{0};
        for (const auto& unaliasedResource : unaliasedResources)
        {
            LOG_INFO("{} costs: {:.4f} MB, has EL: [{}, {}].", unaliasedResource.DebugName,
                     unaliasedResource.MemoryRequirements.size / 1024.f / 1024.f, m_ResourceLifetimeMap[unaliasedResource.ID].Begin,
                     m_ResourceLifetimeMap[unaliasedResource.ID].End);
            memoryUsage += unaliasedResource.MemoryRequirements.size;
        }
        LOG_TRACE("RenderGraphResourcePool: Total Memory Usage {:.2f} MB", memoryUsage / 1024.0f / 1024.0f);

        while (!unaliasedResources.empty())
        {
            bool bResourceAssigned    = false;
            auto resourceToBeAssigned = std::move(unaliasedResources.front());
            unaliasedResources.pop_front();

            for (auto& memoryBucket : m_MemoryBuckets)
            {
                // NOTES:
                // 1) First row's resource in bucket fully occupies it!
                // 2) Memory type should be the same! (but it's already the same since I splitted RMA by memory types)
                if (DoEffectiveLifetimeConflict(m_ResourceLifetimeMap[memoryBucket.StorageOfRows[0][0].ID],
                                                m_ResourceLifetimeMap[resourceToBeAssigned.ID]) /*||
                    resourceToBeAssigned.MemoryPropertyFlags != memoryBucket.StorageOfRows[0][0].MemoryPropertyFlags*/)
                    continue;

                for (auto& row : memoryBucket.StorageOfRows)
                {
                    const auto& lastResourceInRow = row.back();
                    const bool bNeedsNewRow       = lastResourceInRow.Offset + lastResourceInRow.MemoryRequirements.size >=
                                                  memoryBucket.StorageOfRows[0][0].MemoryRequirements.size ||
                                              lastResourceInRow.Offset + resourceToBeAssigned.MemoryRequirements.size >
                                                  memoryBucket.StorageOfRows[0][0].MemoryRequirements.size;
                    if (!bNeedsNewRow)
                    {
                        row.emplace_back(resourceToBeAssigned.ResourceHandle, resourceToBeAssigned.ID,
                                         lastResourceInRow.Offset + lastResourceInRow.MemoryRequirements.size,
                                         resourceToBeAssigned.DebugName, resourceToBeAssigned.MemoryRequirements,
                                         resourceToBeAssigned.MemoryPropertyFlags);
                    }
                    else
                    {
                        auto& newRow = memoryBucket.StorageOfRows.emplace_back();
                        newRow.emplace_back(resourceToBeAssigned.ResourceHandle, resourceToBeAssigned.ID, 0, resourceToBeAssigned.DebugName,
                                            resourceToBeAssigned.MemoryRequirements, resourceToBeAssigned.MemoryPropertyFlags);
                    }

                    bResourceAssigned = true;
                    break;
                }
                if (bResourceAssigned) break;
            }

            if (!bResourceAssigned)
            {
                auto& bucket   = m_MemoryBuckets.emplace_back();
                auto& firstRow = bucket.StorageOfRows.emplace_back();
                firstRow.emplace_back(resourceToBeAssigned.ResourceHandle, resourceToBeAssigned.ID, 0, resourceToBeAssigned.DebugName,
                                      resourceToBeAssigned.MemoryRequirements, resourceToBeAssigned.MemoryPropertyFlags);
            }
        }

        u64 memoryUsageAfterAliasing{0};
        for (const auto& memoryBucket : m_MemoryBuckets)
        {
            memoryUsageAfterAliasing += memoryBucket.StorageOfRows[0][0].MemoryRequirements.size;
        }
        LOG_TRACE("RenderGraphResourcePool: Total Memory Usage After Aliasing {:.2f} MB, Reduction {:.2f}%",
                  memoryUsageAfterAliasing / 1024.0f / 1024.0f,
                  (memoryUsage - memoryUsageAfterAliasing) / static_cast<f32>(memoryUsage) * 100.0f);

        // 1. Gather memory requirements.
        // 2. Bind resource to memory.
        for (auto& memoryBucket : m_MemoryBuckets)
        {
            RDNT_ASSERT(!memoryBucket.StorageOfRows.empty() && !memoryBucket.StorageOfRows.front().empty(), "MemoryBucket is invalid!");
            // NOTE: First row's resource in bucket fully occupies it!
            memoryBucket.MemoryRequirements  = memoryBucket.StorageOfRows[0][0].MemoryRequirements;
            memoryBucket.MemoryPropertyFlags = memoryBucket.StorageOfRows[0][0].MemoryPropertyFlags;

            for (const auto& row : memoryBucket.StorageOfRows)
            {
                for (const auto& resource : row)
                {
                    memoryBucket.MemoryRequirements.alignment =
                        std::max(memoryBucket.MemoryRequirements.alignment, resource.MemoryRequirements.alignment);
                    memoryBucket.MemoryRequirements.memoryTypeBits &= resource.MemoryRequirements.memoryTypeBits;
                    memoryBucket.MemoryPropertyFlags |= resource.MemoryPropertyFlags;
                }
            }

            RDNT_ASSERT(memoryBucket.MemoryRequirements.memoryTypeBits != 0, "Invalid memory type bits!");

            m_ResourcePoolPtr->m_Device->AllocateMemory(memoryBucket.Allocation, memoryBucket.MemoryRequirements,
                                                        memoryBucket.MemoryPropertyFlags);
            for (const auto& row : memoryBucket.StorageOfRows)
            {
                for (const auto& resource : row)
                {
                    if (!m_ResourcesNeededMemoryRebind.contains(resource.ID)) continue;

                    if (auto* rgTextureHandle = std::get_if<RGTextureHandle>(&resource.ResourceHandle))
                    {
                        auto& gfxTextureHandle = m_ResourcePoolPtr->GetTexture(*rgTextureHandle)->Get();
                        m_ResourcePoolPtr->m_Device->BindTexture(*gfxTextureHandle, memoryBucket.Allocation, resource.Offset);
                        gfxTextureHandle->RenderGraph_Finalize();
                    }

                    if (auto* rgBufferHandle = std::get_if<RGBufferHandle>(&resource.ResourceHandle))
                    {
                        auto& gfxBufferHandle = m_ResourcePoolPtr->GetBuffer(*rgBufferHandle)->Get();
                        m_ResourcePoolPtr->m_Device->BindBuffer(*gfxBufferHandle, memoryBucket.Allocation, resource.Offset);
                        gfxBufferHandle->RenderGraph_Finalize(memoryBucket.Allocation);
                    }
                }
            }
        }
        m_ResourcesNeededMemoryRebind.clear();

        LOG_TRACE("Resource aliasing done!");
    }

}  // namespace Radiant
