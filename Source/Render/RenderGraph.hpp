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
    // https://levelup.gitconnected.com/organizing-gpu-work-with-directed-acyclic-graphs-f3fd5f2c2af3
    // https://levelup.gitconnected.com/gpu-memory-aliasing-45933681a15e
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
    // https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples
    // https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/

    class RenderGraph;
    class RenderGraphPass;
    class RenderGraphResourcePool;
    class RenderGraphResourceScheduler;

    // TODO:
    /*using MipList  = std::vector<uint32_t>;
    using MipRange = std::pair<uint32_t, std::optional<uint32_t>>;

    struct MipSet
    {
        static MipSet Empty();
        static MipSet Explicit(const MipList& mips);
        static MipSet IndexFromStart(uint32_t index);
        static MipSet IndexFromEnd(uint32_t index);
        static MipSet FirstMip();
        static MipSet LastMip();
        static MipSet AllMips();
        static MipSet Range(uint32_t firstMip, std::optional<uint32_t> lastMip);

        using MipVariant = std::variant<MipList, MipRange, uint32_t, uint32_t>;

        std::optional<MipVariant> Combination = std::nullopt;
    };*/

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
        ExtraBufferFlags BufferFlags{EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST};
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
        void CreateResources() noexcept;

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

    // NOTE:
    // 1) All CPU-side(ReBAR as well!!) buffers are buffered by default, but GPU-side aren't!
    // 2) All textures aren't buffered!
    class RenderGraphResourcePool final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphResourcePool(const Unique<GfxDevice>& device) noexcept : m_Device(device), m_DeviceRMA(this)
        {
            for (u8 i{}; i < s_BufferedFrameCount; ++i)
            {
                m_ReBARRMA[i] = ResourceMemoryAliaser(this);
                m_HostRMA[i]  = ResourceMemoryAliaser(this);
            }
        }
        ~RenderGraphResourcePool() noexcept
        {
            m_Device->WaitIdle();

            m_DeviceRMA.CleanMemoryBuckets();
            for (u8 i{}; i < s_BufferedFrameCount; ++i)
            {
                m_HostRMA[i].CleanMemoryBuckets();
                m_ReBARRMA[i].CleanMemoryBuckets();
            }
        }

        void Tick() noexcept
        {
            ++m_GlobalFrameNumber;
            m_CurrentFrameIndex = m_GlobalFrameNumber % s_BufferedFrameCount;

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

            constexpr auto BufferTickFunc = [](GfxBufferVector& bufferVector, const u64 globalFrameNumber) noexcept
            {
                for (u32 i{}; i < bufferVector.size();)
                {
                    const u64 framesPast = bufferVector[i].LastUsedFrameIndex + static_cast<u64>(s_BufferedFrameCount);
                    if (framesPast < globalFrameNumber)
                    {
                        std::swap(bufferVector[i], bufferVector.back());
                        bufferVector.pop_back();
                    }
                    else
                    {
                        bufferVector[i].Handle->SetState(EResourceState::RESOURCE_STATE_UNDEFINED);
                        ++i;
                    }
                }
            };

            BufferTickFunc(m_DeviceBuffers, m_GlobalFrameNumber);
            BufferTickFunc(m_HostBuffers[m_CurrentFrameIndex], m_GlobalFrameNumber);
            BufferTickFunc(m_ReBARBuffers[m_CurrentFrameIndex], m_GlobalFrameNumber);
        }

        NODISCARD RGTextureHandle CreateTexture(const GfxTextureDescription& textureDesc, const std::string& textureName,
                                                const RGResourceID& resourceID) noexcept;
        NODISCARD RGBufferHandle CreateBuffer(const GfxBufferDescription& bufferDesc, const std::string& bufferName,
                                              const RGResourceID& resourceID) noexcept;

        NODISCARD FORCEINLINE Unique<RenderGraphResourceTexture>& GetTexture(const RGTextureHandle& handleID) noexcept
        {
            RDNT_ASSERT(handleID < m_Textures.size(), "RGTextureHandle is invalid!");
            return m_Textures[handleID].Handle;
        }

        NODISCARD Unique<RenderGraphResourceBuffer>& GetBuffer(const RGBufferHandle& handleID) noexcept
        {
            RDNT_ASSERT(handleID.BufferFlags != 0 || handleID.BufferFlags == EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL ||
                            handleID.BufferFlags ==
                                (EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST | EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE) ||
                            handleID.BufferFlags == EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR,
                        "Invalid buffer flags!");

            // NOTE: Handling rebar first cuz it contains device and host bits!
            const bool bIsReBAR = (handleID.BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR) ==
                                  EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR;
            if (bIsReBAR)
            {
                RDNT_ASSERT(handleID.ID < m_ReBARBuffers[m_CurrentFrameIndex].size(), "ReBAR: CPU+GPU BufferHandle is invalid!");
                return m_ReBARBuffers[m_CurrentFrameIndex][handleID.ID].Handle;
            }

            const bool bIsDeviceLocal = (handleID.BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) ==
                                        EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL;
            if (bIsDeviceLocal)
            {
                RDNT_ASSERT(handleID.ID < m_DeviceBuffers.size(), "GPUBufferHandle is invalid!");
                return m_DeviceBuffers[handleID.ID].Handle;
            }

            const bool bIsHostVisible =
                (handleID.BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST;
            if (bIsHostVisible)
            {
                RDNT_ASSERT(handleID.ID < m_HostBuffers[m_CurrentFrameIndex].size(), "CPUBufferHandle is invalid!");
                return m_HostBuffers[m_CurrentFrameIndex][handleID.ID].Handle;
            }

            RDNT_ASSERT(false, "{}: Nothing to return!", __FUNCTION__);
        }

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

                const auto el = EffectiveLifetime{.Begin = begin, .End = end};
                if (m_ReBARRMA[m_CurrentFrameIndex].m_ResourceInfoMap.contains(resourceID))
                    m_ReBARRMA[m_CurrentFrameIndex].m_ResourceLifetimeMap[resourceID] = el;
                else if (m_HostRMA[m_CurrentFrameIndex].m_ResourceInfoMap.contains(resourceID))
                    m_HostRMA[m_CurrentFrameIndex].m_ResourceLifetimeMap[resourceID] = el;
                else if (m_DeviceRMA.m_ResourceInfoMap.contains(resourceID))
                    m_DeviceRMA.m_ResourceLifetimeMap[resourceID] = el;
                else
                    RDNT_ASSERT(false, "Unknown resource id?! It isn't present in any resource info map!");
            }
        }

        void FillResourceInfo(const std::variant<RGTextureHandle, RGBufferHandle>& resourceHandle, const RGResourceID& id,
                              const std::string& debugName, const vk::MemoryRequirements& memoryRequirements,
                              const vk::MemoryPropertyFlags memoryPropertyFlags) noexcept
        {
            if (const auto* rgTextureHandle = std::get_if<RGTextureHandle>(&resourceHandle))
            {
                m_DeviceRMA.FillResourceInfo(resourceHandle, id, debugName, memoryRequirements, memoryPropertyFlags);
            }
            else if (const auto* rgBufferHandle = std::get_if<RGBufferHandle>(&resourceHandle))
            {
                const bool bIsReBAR = (rgBufferHandle->BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR) ==
                                      EExtraBufferFlag::EXTRA_BUFFER_FLAG_RESIZABLE_BAR;
                const bool bIsDeviceLocal = (rgBufferHandle->BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) ==
                                            EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL;
                const bool bIsHostVisible =
                    (rgBufferHandle->BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST;

                // NOTE: Handling rebar first cuz it contains device and host bits!
                if (bIsReBAR)
                    m_ReBARRMA[m_CurrentFrameIndex].FillResourceInfo(resourceHandle, id, debugName, memoryRequirements,
                                                                     memoryPropertyFlags);
                else if (bIsHostVisible)
                    m_HostRMA[m_CurrentFrameIndex].FillResourceInfo(resourceHandle, id, debugName, memoryRequirements, memoryPropertyFlags);
                else if (bIsDeviceLocal)
                    m_DeviceRMA.FillResourceInfo(resourceHandle, id, debugName, memoryRequirements, memoryPropertyFlags);
            }
        }
        void BindResourcesToMemoryRegions() noexcept;

      private:
        const Unique<GfxDevice>& m_Device;
        u64 m_GlobalFrameNumber{0};
        u8 m_CurrentFrameIndex{0};

        struct PooledBuffer
        {
            Unique<RenderGraphResourceBuffer> Handle{nullptr};
            u64 LastUsedFrameIndex{};
        };
        using GfxBufferVector = std::vector<PooledBuffer>;

        using GfxBufferVectorPerFrame = std::array<GfxBufferVector, s_BufferedFrameCount>;
        GfxBufferVectorPerFrame m_HostBuffers;
        GfxBufferVectorPerFrame m_ReBARBuffers;
        GfxBufferVector m_DeviceBuffers;

        struct PooledTexture
        {
            Unique<RenderGraphResourceTexture> Handle{nullptr};
            u64 LastUsedFrameIndex{};
        };
        std::vector<PooledTexture> m_Textures;

        // RESOURCE ALIASING

        // NOTE: Each of this in future will represent pass or dependency level(if multiple queues)
        struct EffectiveLifetime
        {
            u32 Begin{0};
            u32 End{0};
        };
        FORCEINLINE static bool DoEffectiveLifetimeConflict(const EffectiveLifetime& lhs, const EffectiveLifetime& rhs) noexcept
        {
            return lhs.Begin <= rhs.End && rhs.Begin <= lhs.End;
        }

        struct ResourceBucket
        {
            vk::MemoryPropertyFlags MemoryPropertyFlags{};
            vk::MemoryRequirements MemoryRequirements{};
            VmaAllocation Allocation{VK_NULL_HANDLE};

            struct OverlappedResource
            {
                std::variant<RGTextureHandle, RGBufferHandle> ResourceHandle{};
                RGResourceID ID{};
                u32 Offset{};
                std::string DebugName{s_DEFAULT_STRING};
                vk::MemoryRequirements MemoryRequirements{};
                vk::MemoryPropertyFlags MemoryPropertyFlags{};
            };

            std::vector<std::vector<OverlappedResource>> StorageOfRows;
        };

        struct RenderGraphResourceInfo
        {
            std::variant<RGTextureHandle, RGBufferHandle> ResourceHandle{};  // to decide which rma should it go
            std::string DebugName{s_DEFAULT_STRING};                         // to simply set debug name and other shit
            vk::MemoryRequirements MemoryRequirements{};                     // to properly choose memory things
            vk::MemoryPropertyFlags MemoryPropertyFlags{};                   // to properly choose memory bucket based on memory type
        };

        struct ResourceMemoryAliaser
        {
            void FillResourceInfo(const std::variant<RGTextureHandle, RGBufferHandle>& resourceHandle, const RGResourceID& resourceID,
                                  const std::string& debugName, const vk::MemoryRequirements& memoryRequirements,
                                  const vk::MemoryPropertyFlags memoryPropertyFlags) noexcept
            {
                // NOTE: Huge check if we need trigger memory reallocation.
                if (m_ResourceInfoMap.contains(resourceID) && !m_ResourcesNeededMemoryRebind.contains(resourceID))
                {
                    bool bResourceHandleDifferent = false;
                    if (m_ResourceInfoMap[resourceID].ResourceHandle.index() == resourceHandle.index())
                    {
                        if (const auto* rgTextureHandleLhs = std::get_if<RGTextureHandle>(&resourceHandle);
                            const auto* rgTextureHandleRhs = std::get_if<RGTextureHandle>(&m_ResourceInfoMap[resourceID].ResourceHandle))
                        {
                            bResourceHandleDifferent = *rgTextureHandleLhs != *rgTextureHandleRhs;
                        }
                        else if (const auto* rgBufferHandleLhs = std::get_if<RGBufferHandle>(&resourceHandle);
                                 const auto* rgBufferHandleRhs = std::get_if<RGBufferHandle>(&m_ResourceInfoMap[resourceID].ResourceHandle))
                        {
                            bResourceHandleDifferent = rgBufferHandleLhs->ID != rgBufferHandleRhs->ID ||
                                                       rgBufferHandleLhs->BufferFlags != rgBufferHandleRhs->BufferFlags;
                        }
                    }
                    else
                        bResourceHandleDifferent = true;

                    const bool bResourceNeedsToBeReallocated = bResourceHandleDifferent ||
                                                               m_ResourceInfoMap[resourceID].MemoryRequirements != memoryRequirements ||
                                                               m_ResourceInfoMap[resourceID].MemoryPropertyFlags != memoryPropertyFlags;
                    if (bResourceNeedsToBeReallocated) m_ResourcesNeededMemoryRebind.emplace(resourceID);
                }

                m_ResourceInfoMap[resourceID] = {.ResourceHandle      = resourceHandle,
                                                 .DebugName           = debugName,
                                                 .MemoryRequirements  = memoryRequirements,
                                                 .MemoryPropertyFlags = memoryPropertyFlags};
            }
            void BindResourcesToMemoryRegions() noexcept;
            void CleanMemoryBuckets() noexcept
            {
                RDNT_ASSERT(m_ResourcePoolPtr, "ResourcePoolPtr isn't valid!");
                for (auto& memoryBucket : m_MemoryBuckets)
                {
                    m_ResourcePoolPtr->m_Device->PushObjectToDelete(
                        [movedAllocation = std::move(memoryBucket.Allocation)]() noexcept
                        { GfxContext::Get().GetDevice()->FreeMemory((VmaAllocation&)movedAllocation); });
                }

                m_MemoryBuckets.clear();
            }

            ResourceMemoryAliaser(RenderGraphResourcePool* resourcePool) noexcept : m_ResourcePoolPtr(resourcePool) {}
            ResourceMemoryAliaser() noexcept  = default;  // NOTE: Shouldn't be used!
            ~ResourceMemoryAliaser() noexcept = default;

            RenderGraphResourcePool* m_ResourcePoolPtr{nullptr};
            UnorderedMap<RGResourceID, RenderGraphResourceInfo> m_ResourceInfoMap;
            UnorderedMap<RGResourceID, EffectiveLifetime> m_ResourceLifetimeMap;
            UnorderedSet<RGResourceID> m_ResourcesNeededMemoryRebind;
            std::vector<ResourceBucket> m_MemoryBuckets;
        };

        using ResourceMemoryAliaserPerFrame = std::array<ResourceMemoryAliaser, s_BufferedFrameCount>;
        ResourceMemoryAliaserPerFrame m_HostRMA;
        ResourceMemoryAliaserPerFrame m_ReBARRMA;
        ResourceMemoryAliaser m_DeviceRMA;

        // RESOURCE ALIASING

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
        NODISCARD void WriteRenderTarget(const std::string& name, const vk::AttachmentLoadOp loadOp, const vk::AttachmentStoreOp storeOp,
                                         const vk::ClearColorValue& clearValue) noexcept;

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
