#pragma once

#include <Render/CoreDefines.hpp>

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>

#include <vector>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <Core/Core.hpp>
#include <Core/CoreTypes.hpp>

namespace Radiant
{
    // https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples
    // https://levelup.gitconnected.com/gpu-memory-aliasing-45933681a15e
    // https://levelup.gitconnected.com/organizing-gpu-work-with-directed-acyclic-graphs-f3fd5f2c2af3
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html

    class RenderGraph;
    class RenderGraphPass;
    class RenderGraphResourcePool;
    class RenderGraphResourceScheduler;

    // TODO:
    //   struct MipSet final
    //{
    //};

    // RenderGraphPass
    enum class ERenderGraphPassType : u8
    {
        RENDER_GRAPH_PASS_TYPE_COMPUTE,
        RENDER_GRAPH_PASS_TYPE_TRANSFER,
        RENDER_GRAPH_PASS_TYPE_GRAPHICS,
        RENDER_GRAPH_PASS_TYPE_RAY_TRACING,
        // TODO:
        RENDER_GRAPH_PASS_TYPE_ASYNC_COMPUTE,
        RENDER_GRAPH_PASS_TYPE_DEDICATED_TRANSFER,
    };

    using RenderGraphSetupFunc   = std::function<void(RenderGraphResourceScheduler&)>;
    using RenderGraphExecuteFunc = std::function<void(const RenderGraphResourceScheduler&, const vk::CommandBuffer&)>;

    // RenderGraphResourcePool
    struct RenderGraphBufferHandle
    {
        u64 ID{0};
        ExtraBufferFlags BufferFlags{EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED};
    };

    using RGTextureHandle = u64;
    using RGBufferHandle  = RenderGraphBufferHandle;

    // RenderGraph
    using RGResourceID = u64;  // Unique resource ID

    struct RenderGraphStatistics
    {
        f32 BuildTime{0.0f};  // CPU build time(milliseconds).
        u32 BarrierBatchCount{0};
        u32 BarrierCount{0};
    };

    class RenderGraph final : private Uncopyable, private Unmovable
    {
      public:
        explicit RenderGraph(const Unique<GfxContext>& gfxContext, const std::string_view& name,
                             Unique<RenderGraphResourcePool>& resourcePool) noexcept
            : m_GfxContext(gfxContext), m_Name(name), m_ResourcePool(resourcePool)
        {
            RDNT_ASSERT(gfxContext && resourcePool, "GfxContext or RenderGraphResourcePool is invalid!");
        }
        ~RenderGraph() noexcept = default;

        class DependencyLevel final
        {
          public:
            DependencyLevel(RenderGraph& renderGraph) noexcept : m_RenderGraph(renderGraph) {}
            ~DependencyLevel() noexcept = default;

            void AddPass(RenderGraphPass* pass) noexcept { m_Passes.emplace_back(pass); }

            void Execute(const Unique<GfxContext>& gfxContext) noexcept;

          private:
            RenderGraph& m_RenderGraph;
            u32 m_LevelIndex{0};
            std::vector<RenderGraphPass*> m_Passes;

            friend RenderGraph;
        };

        void AddPass(const std::string_view& name, const ERenderGraphPassType passType, RenderGraphSetupFunc&& setupFunc,
                     RenderGraphExecuteFunc&& executeFunc) noexcept;

        void Build() noexcept;
        void Execute() noexcept;

        NODISCARD RGResourceID CreateResourceID(const std::string& name) noexcept
        {
            RDNT_ASSERT(!name.empty(), "Resource name is empty!");
            RDNT_ASSERT(!m_ResourceNameToID.contains(name), "Resource[{}] already exists!", name);

            m_ResourceNameToID[name] = m_ResourceIDPool.Emplace(m_ResourceIDPool.GetSize());
            return m_ResourceNameToID[name];
        }

        NODISCARD auto GetResourceID(const std::string& name) const noexcept
        {
            RDNT_ASSERT(!name.empty(), "Resource name is empty!");
            RDNT_ASSERT(m_ResourceNameToID.contains(name), "Resource[{}] doesn't exist!", name);

            return m_ResourceNameToID.at(name);
        }

        NODISCARD Unique<GfxTexture>& GetTexture(const RGResourceID& resourceID) noexcept;
        NODISCARD Unique<GfxBuffer>& GetBuffer(const RGResourceID& resourceID) noexcept;

        auto GetStatistics() const noexcept { return m_Stats; }

      private:
        const Unique<GfxContext>& m_GfxContext;
        std::string m_Name{s_DEFAULT_STRING};
        Unique<RenderGraphResourcePool>& m_ResourcePool;
        RenderGraphStatistics m_Stats = {};

        std::vector<Unique<RenderGraphPass>> m_Passes;
        std::vector<u32> m_TopologicallySortedPassesID;
        std::vector<std::vector<u32>> m_AdjacencyLists;

        std::vector<DependencyLevel> m_DependencyLevels;

        Pool<RGResourceID> m_ResourceIDPool;
        UnorderedMap<std::string, RGResourceID> m_ResourceNameToID;
        // TODO:  UnorderedMap<std::string, std::string> m_AliasMap;  // For RMW things.

        UnorderedMap<RGResourceID, RGTextureHandle> m_ResourceIDToTextureHandle;
        UnorderedMap<RGResourceID, RGBufferHandle> m_ResourceIDToBufferHandle;

        UnorderedMap<std::string, GfxTextureDescription> m_TextureCreates;
        UnorderedMap<std::string, GfxBufferDescription> m_BufferCreates;

        UnorderedMap<RGResourceID, UnorderedSet<u32>>
            m_ResourcesUsedByPassesID{};  // Stores real pass ID, not the one that we get after topsort!

        friend DependencyLevel;
        friend RenderGraphResourceScheduler;
        constexpr RenderGraph() noexcept = delete;
        void BuildAdjacencyLists() noexcept;
        void TopologicalSort() noexcept;
        void BuildDependencyLevels() noexcept;

        void GraphvizDump() const noexcept;
    };

    template <typename TResource> class RenderGraphResource final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphResource(Unique<TResource> resource) noexcept : m_Handle(std::move(resource)) {}
        ~RenderGraphResource() noexcept = default;

        NODISCARD FORCEINLINE auto& Get() noexcept { return m_Handle; }
        NODISCARD FORCEINLINE const auto GetState() const noexcept { return m_CurrentState; }
        FORCEINLINE void SetState(const ResourceStateFlags resourceState) noexcept { m_CurrentState = resourceState; }

      private:
        Unique<TResource> m_Handle{nullptr};
        ResourceStateFlags m_CurrentState{EResourceState::RESOURCE_STATE_UNDEFINED};

        constexpr RenderGraphResource() noexcept = delete;
    };

    using RenderGraphResourceTexture = RenderGraphResource<GfxTexture>;
    using RenderGraphResourceBuffer  = RenderGraphResource<GfxBuffer>;

#if RESOURCE_ALIASING_TODO
    class RenderGraphResourceAliaser final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphResourceAliaser() noexcept  = default;
        ~RenderGraphResourceAliaser() noexcept = default;

        void CalculateEffectiveLifetimes(const std::vector<u32>& topologicallySortedPassesID,
                                         const UnorderedMap<RGResourceID, UnorderedSet<u32>>& resourcesUsedByPassesID) noexcept
        {
            for (const auto& [resourceID, passesIDSet] : resourcesUsedByPassesID)
            {
                u32 begin{std::numeric_limits<u32>::max()};
                u32 end{std::numeric_limits<u32>::min()};

                for (const auto passID : passesIDSet)
                {
                    const auto it = std::find(topologicallySortedPassesID.cbegin(), topologicallySortedPassesID.cend(), passID);
                    RDNT_ASSERT(it != topologicallySortedPassesID.cend(), "Unknown passID!");
                    const auto topsortPassIndex = static_cast<u32>(std::distance(topologicallySortedPassesID.cbegin(), it));
                    begin                       = std::min(begin, topsortPassIndex);
                    end                         = std::max(end, topsortPassIndex);
                }
                m_ResourceLifetimeMap[resourceID] = EffectiveLifetime{.Begin = begin, .End = end};
            }
        }

      private:
        // NOTE: Each of this in future will represent pass or dependency level(if multiple queues)
        struct EffectiveLifetime
        {
            u32 Begin{0};
            u32 End{0};
        };

        UnorderedMap<RGResourceID, EffectiveLifetime> m_ResourceLifetimeMap{};

        //        constexpr RenderGraphResourceAliaser() noexcept = delete;
    };
#endif

    // NOTE:
    // 1) All CPU-side buffers are buffered by default, but GPU-side aren't!
    // 2) All textures aren't buffered!
    class RenderGraphResourcePool final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphResourcePool(const Unique<GfxDevice>& device) noexcept : m_Device(device) {}
        ~RenderGraphResourcePool() noexcept = default;

        void Tick() noexcept
        {
            ++m_GlobalFrameNumber;
            m_CurrentFrameNumber = m_GlobalFrameNumber % s_BufferedFrameCount;

            for (u32 i{}; i < m_Textures.size();)
            {
                const u64 framesPast = m_Textures[i].LastUsedFrameIndex + static_cast<u64>(s_BufferedFrameCount);
                if (framesPast < m_GlobalFrameNumber)
                {
                    std::swap(m_Textures[i], m_Textures.back());
                    m_Textures.pop_back();
                }
                else
                {
                    m_Textures[i].Handle->SetState(EResourceState::RESOURCE_STATE_UNDEFINED);
                    ++i;
                }
            }

            for (u32 i{}; i < m_DeviceBuffers.size();)
            {
                const u64 framesPast = m_DeviceBuffers[i].LastUsedFrameIndex + static_cast<u64>(s_BufferedFrameCount);
                if (framesPast < m_GlobalFrameNumber)
                {
                    std::swap(m_DeviceBuffers[i], m_DeviceBuffers.back());
                    m_DeviceBuffers.pop_back();
                }
                else
                {
                    m_DeviceBuffers[i].Handle->SetState(EResourceState::RESOURCE_STATE_UNDEFINED);
                    ++i;
                }
            }

            auto& currentHostBuffersVector = m_HostBuffers[m_CurrentFrameNumber];
            for (u32 i{}; i < currentHostBuffersVector.size();)
            {
                const u64 framesPast = currentHostBuffersVector[i].LastUsedFrameIndex + static_cast<u64>(s_BufferedFrameCount);
                if (framesPast < m_GlobalFrameNumber)
                {
                    std::swap(currentHostBuffersVector[i], currentHostBuffersVector.back());
                    currentHostBuffersVector.pop_back();
                }
                else
                {
                    currentHostBuffersVector[i].Handle->SetState(EResourceState::RESOURCE_STATE_UNDEFINED);
                    ++i;
                }
            }
        }

        NODISCARD RGTextureHandle CreateTexture(const GfxTextureDescription& textureDesc) noexcept;
        NODISCARD RGBufferHandle CreateBuffer(const GfxBufferDescription& bufferDesc) noexcept;

        NODISCARD FORCEINLINE Unique<RenderGraphResourceTexture>& GetTexture(const RGTextureHandle& handleID) noexcept
        {
            RDNT_ASSERT(handleID < m_Textures.size(), "RGTextureHandle is invalid!");
            return m_Textures[handleID].Handle;
        }

        NODISCARD Unique<RenderGraphResourceBuffer>& GetBuffer(const RGBufferHandle& handleID) noexcept
        {
            RDNT_ASSERT(handleID.BufferFlags != 0 || handleID.BufferFlags == EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL ||
                            handleID.BufferFlags ==
                                (EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED | EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE),
                        "Invalid buffer flags!");

            if ((handleID.BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) ==
                EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL)
            {
                RDNT_ASSERT(handleID.ID < m_DeviceBuffers.size(), "GPUBufferHandle is invalid!");
                return m_DeviceBuffers[handleID.ID].Handle;
            }

            if ((handleID.BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED)
            {
                RDNT_ASSERT(handleID.ID < m_HostBuffers[m_CurrentFrameNumber].size(), "CPUBufferHandle is invalid!");
                return m_HostBuffers[m_CurrentFrameNumber][handleID.ID].Handle;
            }

            RDNT_ASSERT(false, "{}: Nothing to return!", __FUNCTION__);
        }

      private:
        const Unique<GfxDevice>& m_Device;
        u64 m_GlobalFrameNumber{0};
        u8 m_CurrentFrameNumber{0};

        struct PooledBuffer
        {
            Unique<RenderGraphResourceBuffer> Handle{nullptr};
            u64 LastUsedFrameIndex{};
        };
        using GfxBufferVector = std::vector<PooledBuffer>;

        std::array<GfxBufferVector, s_BufferedFrameCount> m_HostBuffers;
        GfxBufferVector m_DeviceBuffers;

        struct PooledTexture
        {
            Unique<RenderGraphResourceTexture> Handle{nullptr};
            u64 LastUsedFrameIndex{};
        };
        std::vector<PooledTexture> m_Textures;

        constexpr RenderGraphResourcePool() noexcept = delete;
    };

    class RenderGraphResourceScheduler final : private Uncopyable, private Unmovable
    {
      public:
        ~RenderGraphResourceScheduler() noexcept = default;

        void CreateTexture(const std::string& name, const GfxTextureDescription& textureDesc) noexcept;
        NODISCARD FORCEINLINE Unique<GfxTexture>& GetTexture(const RGResourceID& resourceID) const noexcept
        {
            return m_RenderGraph.GetTexture(resourceID);
        }

        NODISCARD RGResourceID ReadTexture(const std::string& name, const ResourceStateFlags resourceState) noexcept;
        NODISCARD RGResourceID WriteTexture(const std::string& name, const ResourceStateFlags resourceState) noexcept;
        NODISCARD void WriteDepthStencil(const std::string& name, const vk::AttachmentLoadOp depthLoadOp,
                                                 const vk::AttachmentStoreOp depthStoreOp, const vk::ClearDepthStencilValue& clearValue,
                                                 const vk::AttachmentLoadOp stencilLoadOp   = vk::AttachmentLoadOp::eDontCare,
                                                 const vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eDontCare) noexcept;
        NODISCARD void WriteRenderTarget(const std::string& name, const vk::AttachmentLoadOp loadOp,
                                                 const vk::AttachmentStoreOp storeOp, const vk::ClearColorValue& clearValue) noexcept;

        void CreateBuffer(const std::string& name, const GfxBufferDescription& bufferDesc) noexcept;
        NODISCARD FORCEINLINE Unique<GfxBuffer>& GetBuffer(const RGResourceID& resourceID) const noexcept
        {
            return m_RenderGraph.GetBuffer(resourceID);
        }

        NODISCARD RGResourceID ReadBuffer(const std::string& name, const ResourceStateFlags resourceState) noexcept;
        NODISCARD RGResourceID WriteBuffer(const std::string& name, const ResourceStateFlags resourceState) noexcept;

        void SetViewportScissors(const vk::Viewport& viewport, const vk::Rect2D& scissor) noexcept;

      private:
        RenderGraph& m_RenderGraph;
        RenderGraphPass& m_Pass;

        friend class RenderGraph;
        constexpr RenderGraphResourceScheduler() noexcept = delete;
        RenderGraphResourceScheduler(RenderGraph& renderGraph, RenderGraphPass& pass) noexcept : m_RenderGraph(renderGraph), m_Pass(pass) {}
    };

    class RenderGraphPass final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphPass(const u32 passID, const std::string_view& name, const ERenderGraphPassType passType,
                        RenderGraphSetupFunc&& setupFunc, RenderGraphExecuteFunc&& executeFunc) noexcept
            : m_ID(passID), m_Name(name), m_PassType(passType), m_SetupFunc(setupFunc), m_ExecuteFunc(executeFunc)
        {
        }
        ~RenderGraphPass() noexcept = default;

        void Setup(RenderGraphResourceScheduler& resourceScheduler) const noexcept
        {
            RDNT_ASSERT(m_SetupFunc, "SetupFunc is invalid!");
            m_SetupFunc(resourceScheduler);
        }

        void Execute(RenderGraphResourceScheduler& resourceScheduler, const vk::CommandBuffer& commandBuffer) const noexcept
        {
            RDNT_ASSERT(m_ExecuteFunc, "ExecuteFunc is invalid!");

            if (m_PassType == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
            {
                RDNT_ASSERT(m_Viewport.has_value(), "Viewport is invalid!");
                RDNT_ASSERT(m_Scissor.has_value(), "Scissor is invalid!");

                commandBuffer.setViewport(0, *m_Viewport);
                commandBuffer.setScissor(0, *m_Scissor);
            }

            m_ExecuteFunc(resourceScheduler, commandBuffer);
        }

      private:
        u32 m_ID{0};
        ERenderGraphPassType m_PassType{ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS};
        u32 m_DependencyLevelIndex{0};
        std::string m_Name{s_DEFAULT_STRING};

        RenderGraphSetupFunc m_SetupFunc{};
        RenderGraphExecuteFunc m_ExecuteFunc{};

        UnorderedMap<RGResourceID, ResourceStateFlags> m_ResourceIDToResourceState;

        std::vector<RGResourceID> m_TextureReads;
        std::vector<RGResourceID> m_TextureWrites;

        std::vector<RGResourceID> m_BufferReads;
        std::vector<RGResourceID> m_BufferWrites;

        // TODO: MSAA
        // struct TextureResolveInfo
        // {
        //    RGResourceID ResolveSrc{};
        // vk::AttachmentLoadOp ResolveLoadOp{vk::AttachmentLoadOp::eDontCare};
        // vk::AttachmentStoreOp ResolveStoreOp{vk::AttachmentStoreOp::eDontCare};
        // vk::ResolveModeFlags ResolveMode{vk::ResolveModeFlagBits::eAverage};
        // };

        struct RenderTargetInfo
        {
            std::optional<vk::ClearColorValue> ClearValue{std::nullopt};
            vk::AttachmentLoadOp LoadOp{vk::AttachmentLoadOp::eDontCare};
            vk::AttachmentStoreOp StoreOp{vk::AttachmentStoreOp::eDontCare};
            // std::optional<TextureResolveInfo> ResolveInfo{std::nullopt};
        };

        struct DepthStencilInfo
        {
            std::optional<vk::ClearDepthStencilValue> ClearValue{std::nullopt};
            vk::AttachmentLoadOp DepthLoadOp{vk::AttachmentLoadOp::eDontCare};
            vk::AttachmentStoreOp DepthStoreOp{vk::AttachmentStoreOp::eDontCare};
            vk::AttachmentLoadOp StencilLoadOp{vk::AttachmentLoadOp::eDontCare};
            vk::AttachmentStoreOp StencilStoreOp{vk::AttachmentStoreOp::eDontCare};
            // std::optional<TextureResolveInfo> ResolveInfo{std::nullopt};
        };

        std::vector<RenderTargetInfo> m_RenderTargetInfos;
        std::optional<DepthStencilInfo> m_DepthStencilInfo{std::nullopt};
        std::optional<vk::Viewport> m_Viewport{std::nullopt};
        std::optional<vk::Rect2D> m_Scissor{std::nullopt};

        friend RenderGraph;
        friend RenderGraph::DependencyLevel;
        friend RenderGraphResourceScheduler;
        constexpr RenderGraphPass() noexcept = delete;
    };

}  // namespace Radiant
