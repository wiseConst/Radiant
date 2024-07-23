#include <pch.h>
#include "RenderGraph.hpp"

namespace Radiant
{
    namespace RenderGraphUtils
    {

        static void DepthFirstSearch(std::uint32_t passID, std::vector<std::uint32_t>& sortedPassID,
                                     const std::vector<std::vector<std::uint32_t>>& adjacencyLists,
                                     std::vector<std::uint8_t>& visitedPasses) noexcept
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

    }  // namespace RenderGraphUtils

    void RenderGraph::Compile() noexcept
    {
        RDNT_ASSERT(!m_Passes.empty(), "RenderGraph is empty!");

        BuildAdjacencyLists();
        TopologicalSort();
        GraphvizDump();
        BuildDependencyLevels();
    }

    void RenderGraph::AddPass(const std::string_view& name, const ERenderGraphPassType passType, RenderGraphSetupFunc&& setupFunc,
                              RenderGraphExecuteFunc&& executeFunc) noexcept
    {
        auto& pass = m_Passes.emplace_back(MakeUnique<RenderGraphPass>(static_cast<std::uint32_t>(m_Passes.size()), name, passType,
                                                                       std::forward<RenderGraphSetupFunc>(setupFunc),
                                                                       std::forward<RenderGraphExecuteFunc>(executeFunc)));
        RenderGraphResourceScheduler scheduler(*this, *pass);
        pass->Setup(scheduler);
    }

    void RenderGraph::Execute() noexcept
    {
        Compile();

        auto& frameData = m_GfxContext->GetCurrentFrameData();

        frameData.GeneralCommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        // TODO: Utilize dependency levels
        // NOTE: Sort passes by types in dependency levels to make better utilize warp occupancy for compute passes in a row.
        // for (auto& dependencyLevel : m_DependencyLevels)
        //{
        //    // Sort out pipeline barriers

        //    dependencyLevel.Execute(frameData.GeneralCommandBuffer);
        //}

        for (const auto passIndex : m_SortedPassID)
        {
            auto& currentPass = m_Passes[passIndex];

            for (auto& [textureName, textureDesc] : currentPass->m_TextureCreates)
            {
                m_ResourceIDToTextureHandle[GetResourceID(textureName)] = m_ResourcePool->CreateTexture(textureDesc);
                const vk::Image& image = *m_ResourcePool->GetTexture(m_ResourceIDToTextureHandle[GetResourceID(textureName)])->Get();
                m_GfxContext->GetDevice()->SetDebugName(textureName, image);
            }

            if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
            {
                RDNT_ASSERT(currentPass->m_Viewport.has_value(), "Viewport is invalid!");

                std::vector<vk::ImageMemoryBarrier2> imageMemoryBarriers;

                // TODO: Fill stencil
                auto stencilAttachmentInfo = vk::RenderingAttachmentInfo();
                auto depthAttachmentInfo   = vk::RenderingAttachmentInfo();
                std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos;

                for (auto& resourceID : currentPass->m_TextureReads)
                {
                    auto& RGtexture = m_ResourcePool->GetTexture(m_ResourceIDToTextureHandle[resourceID]);
                    auto& texture   = RGtexture->Get();

                    auto& imageMemoryBarrier = imageMemoryBarriers.emplace_back(vk::ImageMemoryBarrier2().setImage(*texture));

                    const auto currentState = RGtexture->GetState();
                    if (currentState == EResourceState::RESOURCE_STATE_RENDER_TARGET)
                    {
                        imageMemoryBarrier.setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                            .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                            .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal);
                    }

                    if (currentState == EResourceState::RESOURCE_STATE_UNDEFINED)
                    {
                        imageMemoryBarrier.setSrcAccessMask(vk::AccessFlagBits2::eNone)
                            .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                            .setOldLayout(vk::ImageLayout::eUndefined);
                    }

                    const auto nextState = currentPass->m_ResourceIDToResourceState[resourceID];
                    vk::ImageLayout nextLayout{vk::ImageLayout::eUndefined};
                    /*  if (nextState == EResourceState::RESOURCE_STATE_DEPTH_READ)
                      {
                          nextLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                          imageMemoryBarrier.setDstAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentRead)
                              .setDstStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                               vk::PipelineStageFlagBits2::eLateFragmentTests)
                              .setNewLayout(nextLayout);
                      }*/

                    if (nextState == EResourceState::RESOURCE_STATE_COPY_SOURCE)
                    {
                        nextLayout = vk::ImageLayout::eTransferSrcOptimal;
                        imageMemoryBarrier.setDstAccessMask(vk::AccessFlagBits2::eTransferRead)
                            .setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
                            .setNewLayout(nextLayout);
                    }

                    RGtexture->SetState(nextState);
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
                }

                for (auto& resourceID : currentPass->m_TextureWrites)
                {
                    auto& RGtexture = m_ResourcePool->GetTexture(m_ResourceIDToTextureHandle[resourceID]);
                    auto& texture   = RGtexture->Get();

                    auto& imageMemoryBarrier = imageMemoryBarriers.emplace_back(vk::ImageMemoryBarrier2().setImage(*texture));

                    const auto currentState = RGtexture->GetState();
                    if (currentState == EResourceState::RESOURCE_STATE_UNDEFINED)
                    {
                        imageMemoryBarrier.setSrcAccessMask(vk::AccessFlagBits2::eNone)
                            .setSrcStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
                            .setOldLayout(vk::ImageLayout::eUndefined);
                    }

                    const auto nextState = currentPass->m_ResourceIDToResourceState[resourceID];
                    vk::ImageLayout nextLayout{vk::ImageLayout::eUndefined};
                    if (nextState == (EResourceState::RESOURCE_STATE_DEPTH_READ | EResourceState::RESOURCE_STATE_DEPTH_WRITE))
                    {
                        nextLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                        imageMemoryBarrier
                            .setDstAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
                                              vk::AccessFlagBits2::eDepthStencilAttachmentRead)
                            .setDstStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                             vk::PipelineStageFlagBits2::eLateFragmentTests)
                            .setNewLayout(nextLayout);
                    }

                    if (nextState == EResourceState::RESOURCE_STATE_RENDER_TARGET)
                    {
                        nextLayout = vk::ImageLayout::eColorAttachmentOptimal;
                        imageMemoryBarrier.setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                            .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                            .setNewLayout(nextLayout);
                    }

                    RGtexture->SetState(nextState);
                    if (texture->IsDepthFormat(texture->GetDescription().Format))
                    {
                        imageMemoryBarrier.setSubresourceRange(vk::ImageSubresourceRange()
                                                                   .setBaseArrayLayer(0)
                                                                   .setLayerCount(1)
                                                                   .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                                                                   .setBaseMipLevel(0)
                                                                   .setLevelCount(1));

                        depthAttachmentInfo = texture->GetRenderingAttachmentInfo(
                            nextLayout, vk::ClearValue().setDepthStencil(*currentPass->m_DepthStencilClearValue),
                            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore);
                    }
                    else
                    {
                        imageMemoryBarrier.setSubresourceRange(vk::ImageSubresourceRange()
                                                                   .setBaseArrayLayer(0)
                                                                   .setLayerCount(1)
                                                                   .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                   .setBaseMipLevel(0)
                                                                   .setLevelCount(1));

                        colorAttachmentInfos.emplace_back() =
                            texture->GetRenderingAttachmentInfo(nextLayout, vk::ClearValue().setColor(*currentPass->m_ColorClearValue),
                                                                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore);
                    }
                }

                frameData.GeneralCommandBuffer.pipelineBarrier2(
                    vk::DependencyInfo().setDependencyFlags(vk::DependencyFlagBits::eByRegion).setImageMemoryBarriers(imageMemoryBarriers));

                if (currentPass->m_DepthStencilClearValue.has_value() || currentPass->m_ColorClearValue.has_value())
                {
                    frameData.GeneralCommandBuffer.beginRendering(
                        vk::RenderingInfo()
                            .setColorAttachments(colorAttachmentInfos)
                            .setLayerCount(1)
                            .setPDepthAttachment(&depthAttachmentInfo)
                            .setPStencilAttachment(&stencilAttachmentInfo)
                            .setRenderArea(vk::Rect2D().setExtent(
                                vk::Extent2D().setWidth(currentPass->m_Viewport->width).setHeight(currentPass->m_Viewport->height))));
                }
            }

            RenderGraphResourceScheduler scheduler(*this, *currentPass);
            currentPass->ExecuteFunc(scheduler, frameData.GeneralCommandBuffer);

            if (currentPass->m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
            {
                if (currentPass->m_DepthStencilClearValue.has_value() || currentPass->m_ColorClearValue.has_value())
                    frameData.GeneralCommandBuffer.endRendering();
            }
        }

        frameData.GeneralCommandBuffer.end();
    }

    NODISCARD Unique<GfxTexture>& RenderGraph::GetTexture(const RGResourceID& resourceID) noexcept
    {
        RDNT_ASSERT(m_ResourceIDToTextureHandle.contains(resourceID), "ResourceID isn't present in ResourceIDToTextureHandle map!");
        return m_ResourcePool->GetTexture(m_ResourceIDToTextureHandle[resourceID])->Get();
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

                // Try to find textures.
                for (const auto& outTexture : writePass->m_TextureWrites)
                {
                    // If other node reads a subresource written by the current node,
                    // then it depends on current node and is an adjacent dependency
                    bAnyDependencyFound = std::find(readPass->m_TextureReads.cbegin(), readPass->m_TextureReads.cend(), outTexture) !=
                                          readPass->m_TextureReads.cend();

                    if (bAnyDependencyFound) break;
                }
                if (bAnyDependencyFound)
                {
                    m_AdjacencyLists[writePass->m_ID].emplace_back(readPass->m_ID);
                    continue;
                }

                //// Try to find buffers.
                // for (const auto& outBuffer : writePass->m_BufferWrites)
                //{
                //     bAnyDependencyFound =
                //         std::find_if(readPass->m_BufferReads.cbegin(), readPass->m_BufferReads.cend(),
                //                      [&outBuffer](const RGBufferHandle& inputBuffer) {
                //                          return outBuffer.ID == inputBuffer.ID && outBuffer.BufferFlags == inputBuffer.BufferFlags;
                //                      }) != readPass->m_BufferReads.cend();

                //    if (bAnyDependencyFound) break;
                //}
                // if (bAnyDependencyFound) m_AdjacencyLists[writePass->m_ID].emplace_back(readPass->m_ID);
            }
            m_AdjacencyLists[writePass->m_ID].shrink_to_fit();
        }
    }

    void RenderGraph::BuildDependencyLevels() noexcept
    {
        std::vector<std::uint32_t> longestPassDistances(m_SortedPassID.size(), 0);
        std::uint32_t dependencyLevelCount{1};

        // 1. Perform longest node distance search for each node.
        for (auto passID : m_SortedPassID)
        {
            for (auto adjacentPassID : m_AdjacencyLists[passID])
            {
                if (longestPassDistances[adjacentPassID] < longestPassDistances[passID] + 1)
                {
                    const auto newLongestDistance        = longestPassDistances[passID] + 1;
                    longestPassDistances[adjacentPassID] = newLongestDistance;
                    dependencyLevelCount                 = std::max(newLongestDistance + 1, dependencyLevelCount);
                }
            }
        }

        m_DependencyLevels.resize(dependencyLevelCount, *this);

        // 2. Fill dependency levels.
        // Dispatch nodes to corresponding dependency levels.
        // Iterate through unordered nodes because adjacency lists contain indices to
        // initial unordered list of nodes and longest distances also correspond to them.
        for (std::uint32_t passIndex{0}; passIndex < m_Passes.size(); ++passIndex)
        {
            const auto levelIndex        = longestPassDistances[passIndex];
            auto& dependencyLevel        = m_DependencyLevels[levelIndex];
            dependencyLevel.m_LevelIndex = levelIndex;
            dependencyLevel.AddPassIndex(passIndex);

            m_Passes[passIndex]->m_DependencyLevelIndex = levelIndex;
        }
    }

    void RenderGraph::TopologicalSort() noexcept
    {
        std::vector<std::uint8_t> visitedPasses(m_Passes.size(), 0);

        m_SortedPassID.reserve(m_Passes.size());
        for (const auto& pass : m_Passes)
        {
            if (visitedPasses[pass->m_ID] != 2)
                RenderGraphUtils::DepthFirstSearch(pass->m_ID, m_SortedPassID, m_AdjacencyLists, visitedPasses);
        }

        std::ranges::reverse(m_SortedPassID);
    }

    void RenderGraph::GraphvizDump() const noexcept
    {
        RDNT_ASSERT(!m_Passes.empty() && !m_Name.empty(), "DebugName or passes array is not valid!");

        std::stringstream ss;
        ss << "digraph " << m_Name << " {" << std::endl;
        ss << "\tnode [shape=rectangle, style=filled];" << std::endl;
        ss << "\tedge [color=black];" << std::endl << std::endl;

        for (const auto passIndex : m_SortedPassID)
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

    RGResourceID RenderGraphResourceScheduler::WriteDepthStencil(const std::string& name,
                                                                 const vk::ClearDepthStencilValue& clearValue) noexcept
    {
        WriteTexture(name, EResourceState::RESOURCE_STATE_DEPTH_READ | EResourceState::RESOURCE_STATE_DEPTH_WRITE);
        m_Pass.m_DepthStencilClearValue = clearValue;

        return m_RenderGraph.GetResourceID(name);
    }

    RGResourceID RenderGraphResourceScheduler::WriteRenderTarget(const std::string& name, const vk::ClearColorValue& clearValue) noexcept
    {
        WriteTexture(name, EResourceState::RESOURCE_STATE_RENDER_TARGET);
        m_Pass.m_ColorClearValue = clearValue;

        return m_RenderGraph.GetResourceID(name);
    }

    RGResourceID RenderGraphResourceScheduler::ReadTexture(const std::string& name, const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);
        m_Pass.m_TextureReads.emplace_back(resourceID);
        m_Pass.m_ResourceIDToResourceState[resourceID] |= resourceState;

        return resourceID;
    }

    RGResourceID RenderGraphResourceScheduler::WriteTexture(const std::string& name, const ResourceStateFlags resourceState) noexcept
    {
        const auto resourceID = m_RenderGraph.GetResourceID(name);
        m_Pass.m_TextureWrites.emplace_back(resourceID);
        m_Pass.m_ResourceIDToResourceState[resourceID] |= resourceState;

        return resourceID;
    }

    void RenderGraphResourceScheduler::CreateTexture(const std::string& name, const GfxTextureDescription& textureDesc) noexcept
    {
        const auto resourceID                          = m_RenderGraph.CreateResourceID(name);
        m_Pass.m_TextureCreates[name]                  = textureDesc;
        m_Pass.m_ResourceIDToResourceState[resourceID] = EResourceState::RESOURCE_STATE_UNDEFINED;
    }

    NODISCARD Unique<GfxTexture>& RenderGraphResourceScheduler::GetTexture(const RGResourceID& resourceID) noexcept
    {
        return m_RenderGraph.GetTexture(resourceID);
    }

    void RenderGraphResourceScheduler::SetViewportScissors(const vk::Viewport& viewport, const vk::Rect2D& scissor) noexcept
    {
        m_Pass.m_Viewport = viewport;
        m_Pass.m_Scissor  = scissor;
    }

    NODISCARD RGTextureHandle RenderGraphResourcePool::CreateTexture(const GfxTextureDescription& textureDesc) noexcept
    {
        // TODO: Better aliasing techniques

        RGTextureHandle handleID{0};
        for (auto& [RGTexture, lastUsedFrame] : m_Textures)
        {
            if (lastUsedFrame == m_GlobalFrameNumber && RGTexture->Get()->GetDescription() != textureDesc)
            {
                ++handleID;
                continue;
            }

            lastUsedFrame = m_GlobalFrameNumber;
            RGTexture->Get()->Resize(textureDesc.Dimensions);
            return handleID;
        }

        handleID = m_Textures.size();
        m_Textures.emplace_back(MakeUnique<RenderGraphResourceTexture>(MakeUnique<GfxTexture>(m_Device, textureDesc)), m_GlobalFrameNumber);
        return handleID;
    }

    NODISCARD RGBufferHandle RenderGraphResourcePool::CreateBuffer(const GfxBufferDescription& bufferDesc) noexcept
    {
        // TODO: Better aliasing techniques

        RGBufferHandle handleID = {};
        if ((bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL)
        {
            for (auto& [RGBuffer, lastUsedFrame] : m_DeviceBuffers)
            {
                if (lastUsedFrame == m_GlobalFrameNumber || RGBuffer->Get()->GetDescription() != bufferDesc)
                {
                    ++handleID.ID;
                    continue;
                }

                handleID.BufferFlags = bufferDesc.ExtraFlags;
                lastUsedFrame        = m_GlobalFrameNumber;
                RGBuffer->Get()->Resize(bufferDesc.Capacity, bufferDesc.ElementSize);
                return handleID;
            }

            handleID = RenderGraphBufferHandle{.ID = m_DeviceBuffers.size(), .BufferFlags = bufferDesc.ExtraFlags};
            m_DeviceBuffers.emplace_back(MakeUnique<RenderGraphResourceBuffer>(MakeUnique<GfxBuffer>(m_Device, bufferDesc)),
                                         m_GlobalFrameNumber);
            return handleID;
        }

        handleID = {};
        if ((bufferDesc.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED)
        {
            for (auto& [RGBuffer, lastUsedFrame] : m_HostBuffers[m_CurrentFrameNumber])
            {
                if (lastUsedFrame == m_GlobalFrameNumber || RGBuffer->Get()->GetDescription() != bufferDesc)
                {
                    ++handleID.ID;
                    continue;
                }

                handleID.BufferFlags = bufferDesc.ExtraFlags;
                lastUsedFrame        = m_GlobalFrameNumber;
                RGBuffer->Get()->Resize(bufferDesc.Capacity, bufferDesc.ElementSize);
                return handleID;
            }

            handleID = RenderGraphBufferHandle{.ID = m_HostBuffers[m_CurrentFrameNumber].size(), .BufferFlags = bufferDesc.ExtraFlags};
            m_HostBuffers[m_CurrentFrameNumber].emplace_back(
                MakeUnique<RenderGraphResourceBuffer>(MakeUnique<GfxBuffer>(m_Device, bufferDesc)), m_GlobalFrameNumber);
            return handleID;
        }

        RDNT_ASSERT(false, "{}: nothing to return!");
    }

}  // namespace Radiant
