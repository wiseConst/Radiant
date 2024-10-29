#pragma once

#include <Render/RenderGraphDefines.hpp>

template <> struct ankerl::unordered_dense::hash<Radiant::RenderGraphSubresourceID>
{
    using is_avalanching = void;

    [[nodiscard]] auto operator()(const Radiant::RenderGraphSubresourceID& x) const noexcept -> std::uint64_t
    {
        return detail::wyhash::hash(static_cast<std::uint64_t>(x.ResourceID) + static_cast<std::uint64_t>(x.SubresourceIndex));
    }
};

namespace Radiant
{
    // NOTE: Huge thanks to Pavlo Muratov for giga chad article!
    // https://levelup.gitconnected.com/organizing-gpu-work-with-directed-acyclic-graphs-f3fd5f2c2af3
    // https://levelup.gitconnected.com/gpu-memory-aliasing-45933681a15e
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
    // https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples
    // https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/

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

            void PollClearsOnExecute(const vk::CommandBuffer& cmd) noexcept;
            void TransitionResourceStates(const vk::CommandBuffer& cmd) noexcept;
        };

        void AddPass(const std::string_view& name, const ERenderGraphPassType passType, RenderGraphSetupFunc&& setupFunc,
                     RenderGraphExecuteFunc&& executeFunc, const u8 commandQueueIndex = 0) noexcept;

        void Build() noexcept;
        void Execute() noexcept;

        NODISCARD RGResourceID CreateResourceID(const std::string& name) noexcept
        {
            RDNT_ASSERT(!name.empty(), "Resource name is empty!");
            RDNT_ASSERT(!m_ResourceNameToID.contains(name), "Resource[{}] already exists!", name);

            const auto resourceID          = m_ResourceIDPool.Emplace(m_ResourceIDPool.GetSize());
            m_ResourceNameToID[name]       = resourceID;
            m_ResourceIDToName[resourceID] = name;

            return m_ResourceNameToID[name];
        }

        NODISCARD const std::string& ResolveResourceName(const std::string& name) const noexcept
        {
            if (m_ResourceNameToID.contains(name)) return name;

            std::string resourceName{name};
            while (!m_ResourceNameToID.contains(resourceName))
            {
                RDNT_ASSERT(m_ResourceAliasMap.contains(resourceName), "Resource[{}] doesn't exist!", name);
                const auto& aliasedName = m_ResourceAliasMap.at(resourceName);

                if (m_ResourceNameToID.contains(aliasedName)) break;
                resourceName = aliasedName;
            }

            return m_ResourceAliasMap.at(resourceName);
        }

        NODISCARD auto GetResourceID(const std::string& name) const noexcept
        {
            RDNT_ASSERT(!name.empty(), "Resource name is empty!");
            return m_ResourceNameToID.at(ResolveResourceName(name));
        }

        NODISCARD Unique<GfxTexture>& GetTexture(const RGResourceID& resourceID) noexcept;
        NODISCARD Unique<GfxBuffer>& GetBuffer(const RGResourceID& resourceID) noexcept;
        FORCEINLINE u32 GetTextureMipCount(const std::string& name) const noexcept
        {
            const auto& resourceName = ResolveResourceName(name);
            RDNT_ASSERT(m_TextureCreates.contains(resourceName), "Texture[{}] doesn't exist!");

            const auto& dimensions = m_TextureCreates.at(resourceName).Dimensions;
            return GfxTextureUtils::GetMipLevelCount(dimensions.x, dimensions.y);
        }

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
        // NOTE: RMW things.
        UnorderedMap<RGResourceID, std::string> m_ResourceIDToName;
        UnorderedMap<std::string, std::string> m_ResourceAliasMap;

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
        RenderGraphResource(Unique<TResource> resource) noexcept : m_Handle(std::move(resource))
        {
            m_CurrentState[0] = EResourceStateBits::RESOURCE_STATE_UNDEFINED;
        }
        ~RenderGraphResource() noexcept = default;

        NODISCARD FORCEINLINE auto& Get() noexcept { return m_Handle; }
        NODISCARD FORCEINLINE const ResourceStateFlags GetState(const u32 subresourceIndex = 0) const noexcept
        {
            if (!m_CurrentState.contains(subresourceIndex)) return EResourceStateBits::RESOURCE_STATE_UNDEFINED;

            return m_CurrentState.at(subresourceIndex);
        }
        FORCEINLINE void SetState(const ResourceStateFlags resourceState, const u32 subresourceIndex = 0) noexcept
        {
            m_CurrentState[subresourceIndex] = resourceState;
        }
        FORCEINLINE void ResetState() noexcept
        {
            for (auto& [subresourceIndex, resourceState] : m_CurrentState)
                resourceState = EResourceStateBits::RESOURCE_STATE_UNDEFINED;
        }

      private:
        Unique<TResource> m_Handle{nullptr};
        UnorderedMap<u32, ResourceStateFlags> m_CurrentState{EResourceStateBits::RESOURCE_STATE_UNDEFINED};  // NOTE: Per subresource

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
            if constexpr (s_bUseResourceMemoryAliasing)
            {
                m_Device->WaitIdle();

                m_DeviceRMA.CleanMemoryBuckets();
                for (u8 i{}; i < s_BufferedFrameCount; ++i)
                {
                    m_HostRMA[i].CleanMemoryBuckets();
                    m_ReBARRMA[i].CleanMemoryBuckets();
                }
            }
        }

        // NOTE: Usable only when s_bUseResourceMemoryAliasing is true.
        void UI_ShowResourceUsage() const noexcept;

        void Tick() noexcept
        {
            ++m_GlobalFrameNumber;
            m_CurrentFrameIndex = m_GlobalFrameNumber % s_BufferedFrameCount;

            for (auto it = m_Textures.begin(); it != m_Textures.end();)
            {
                const u64 framesPast = it->LastUsedFrameIndex + static_cast<u64>(s_BufferedFrameCount);
                if (framesPast < m_GlobalFrameNumber)
                {
                    it = m_Textures.erase(it);
                }
                else
                {
                    it->Handle->ResetState();
                    ++it;
                }
            }

            constexpr auto BufferTickFunc = [](GfxBufferVector& bufferVector, const u64 globalFrameNumber) noexcept
            {
                for (auto it = bufferVector.begin(); it != bufferVector.end();)
                {
                    const u64 framesPast = it->LastUsedFrameIndex + static_cast<u64>(s_BufferedFrameCount);
                    if (framesPast < globalFrameNumber)
                    {
                        it = bufferVector.erase(it);
                    }
                    else
                    {
                        it->Handle->ResetState();
                        ++it;
                    }
                }
            };

            BufferTickFunc(m_DeviceBuffers, m_GlobalFrameNumber);
            BufferTickFunc(m_HostBuffers[m_CurrentFrameIndex], m_GlobalFrameNumber);
            BufferTickFunc(m_ReBARBuffers[m_CurrentFrameIndex], m_GlobalFrameNumber);

            m_DeviceRMA.ClearState();
            m_HostRMA[m_CurrentFrameIndex].ClearState();
            m_ReBARRMA[m_CurrentFrameIndex].ClearState();
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
            RDNT_ASSERT(handleID.BufferFlags != 0 || handleID.BufferFlags == EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT ||
                            handleID.BufferFlags == (EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT |
                                                     EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_ADDRESSABLE_BIT) ||
                            handleID.BufferFlags == EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT ||
                            handleID.BufferFlags == EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT,
                        "Invalid buffer flags!");

            // NOTE: Handling rebar first cuz it contains device and host bits!
            if (handleID.BufferFlags == EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT)
            {
                RDNT_ASSERT(handleID.ID < m_ReBARBuffers[m_CurrentFrameIndex].size(), "ReBAR: CPU+GPU BufferHandle is invalid!");
                return m_ReBARBuffers[m_CurrentFrameIndex][handleID.ID].Handle;
            }

            if (handleID.BufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)
            {
                RDNT_ASSERT(handleID.ID < m_DeviceBuffers.size(), "GPUBufferHandle is invalid!");
                return m_DeviceBuffers[handleID.ID].Handle;
            }

            if (handleID.BufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT)
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

                const auto el = RenderGraphResourceEffectiveLifetime{.Begin = begin, .End = end};
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

        void FillResourceInfo(const RGResourceHandleVariant& resourceHandle, const RGResourceID& resourceID, const std::string& debugName,
                              const vk::MemoryRequirements& memoryRequirements, const vk::MemoryPropertyFlags memoryPropertyFlags) noexcept
        {
            if (const auto* rgTextureHandle = std::get_if<RGTextureHandle>(&resourceHandle))
            {
                m_DeviceRMA.FillResourceInfo(resourceHandle, resourceID, debugName, memoryRequirements, memoryPropertyFlags);
            }
            else if (const auto* rgBufferHandle = std::get_if<RGBufferHandle>(&resourceHandle))
            {
                // NOTE: Handling rebar first cuz it contains device and host bits!
                if (rgBufferHandle->BufferFlags == EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT)
                    m_ReBARRMA[m_CurrentFrameIndex].FillResourceInfo(resourceHandle, resourceID, debugName, memoryRequirements,
                                                                     memoryPropertyFlags);
                else if (rgBufferHandle->BufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT)
                    m_HostRMA[m_CurrentFrameIndex].FillResourceInfo(resourceHandle, resourceID, debugName, memoryRequirements,
                                                                    memoryPropertyFlags);
                else if (rgBufferHandle->BufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)
                    m_DeviceRMA.FillResourceInfo(resourceHandle, resourceID, debugName, memoryRequirements, memoryPropertyFlags);
            }
        }

        FORCEINLINE void BindResourcesToMemoryRegions() noexcept
        {
            m_DeviceRMA.BindResourcesToMemoryRegions();
            m_ReBARRMA[m_CurrentFrameIndex].BindResourcesToMemoryRegions();
            m_HostRMA[m_CurrentFrameIndex].BindResourcesToMemoryRegions();
        }

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

        struct RenderGraphResourceInfo
        {
            RGResourceHandleVariant ResourceHandle{};       // To decide which RMA should it go.
            std::string DebugName{s_DEFAULT_STRING};        // To simply set debug name.
            vk::MemoryRequirements MemoryRequirements{};    // To properly choose memory allocation size of bucket it'll be assigned to.
            vk::MemoryPropertyFlags MemoryPropertyFlags{};  // To properly choose memory bucket based on memory type.
        };

        struct RenderGraphResourceBucket
        {
            vk::MemoryPropertyFlags MemoryPropertyFlags{};
            vk::MemoryRequirements MemoryRequirements{};
            VmaAllocation Allocation{VK_NULL_HANDLE};

            struct RenderGraphOverlappedResource
            {
                RenderGraphResourceInfo ResourceInfo{};
                RGResourceID ResourceID{};
                u64 Offset{};
            };

            std::vector<RenderGraphOverlappedResource> AlreadyAliasedResources;
        };

        // NOTE: Now it represents pass IDs, but each of this in future will represent pass or dependency level(if multiple queues)
        struct RenderGraphResourceEffectiveLifetime
        {
            u32 Begin{};
            u32 End{};
        };

        struct ResourceMemoryAliaser final
        {
          public:
            ResourceMemoryAliaser(RenderGraphResourcePool* resourcePool) noexcept : m_ResourcePoolPtr(resourcePool) {}
            ResourceMemoryAliaser() noexcept  = default;  // NOTE: Shouldn't be used!
            ~ResourceMemoryAliaser() noexcept = default;

            FORCEINLINE void FillResourceInfo(const RGResourceHandleVariant& resourceHandle, const RGResourceID& resourceID,
                                              const std::string& debugName, const vk::MemoryRequirements& memoryRequirements,
                                              const vk::MemoryPropertyFlags memoryPropertyFlags) noexcept
            {
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

            void ClearState() noexcept
            {
                m_ResourceInfoMap.clear();
                m_ResourceLifetimeMap.clear();
                m_ResourcesNeededMemoryRebind.clear();
            }

            FORCEINLINE bool DoEffectiveLifetimesIntersect(const RenderGraphResourceEffectiveLifetime& lhs,
                                                           const RenderGraphResourceEffectiveLifetime& rhs) noexcept
            {
                return lhs.Begin <= rhs.End && rhs.Begin <= lhs.End;
            }

            struct RenderGraphResourceUnaliased
            {
                RenderGraphResourceInfo ResourceInfo{};
                RGResourceID ResourceID{};
            };
            // Invalidates all resources present in resource map, and returns ASC sorted by size array of resources to be aliased.
            NODISCARD std::vector<RenderGraphResourceUnaliased> GetUnaliasedResourcesList(const bool bNeedMemoryDefragmentation) noexcept;

            enum class EMemoryOffsetType : u8
            {
                MEMORY_OFFSET_TYPE_START,
                MEMORY_OFFSET_TYPE_END
            };
            using MemoryOffset = std::pair<u64, EMemoryOffsetType>;

            struct MemoryRegion
            {
                u64 Offset{};  // Bytes
                u64 Size{};    // Bytes
            };
            NODISCARD std::optional<MemoryRegion> FindBestMemoryRegion(const std::vector<MemoryOffset>& nonAliasableMemoryOffsetList,
                                                                       const RenderGraphResourceBucket& memoryBucket,
                                                                       const RenderGraphResourceUnaliased& resourceToBeAssigned) noexcept;

            // Returns non-aliasable memory offsets for every resource,
            // each time we wanna emplace new resource.
            // ASC sorts "u64" so-called memory offsets bytes.
            NODISCARD std::vector<MemoryOffset> BuildNonAliasableMemoryOffsetList(
                const RenderGraphResourceBucket& memoryBucket, const RenderGraphResourceUnaliased& resourceToBeAssigned) noexcept;

            RenderGraphResourcePool* m_ResourcePoolPtr{nullptr};
            UnorderedMap<RGResourceID, RenderGraphResourceInfo> m_ResourceInfoMap;
            UnorderedMap<RGResourceID, RenderGraphResourceEffectiveLifetime> m_ResourceLifetimeMap;
            UnorderedSet<RGResourceID> m_ResourcesNeededMemoryRebind;
            std::vector<RenderGraphResourceBucket> m_MemoryBuckets;
        };

        using ResourceMemoryAliaserPerFrame = std::array<ResourceMemoryAliaser, s_BufferedFrameCount>;
        ResourceMemoryAliaserPerFrame m_HostRMA;
        ResourceMemoryAliaserPerFrame m_ReBARRMA;
        ResourceMemoryAliaser m_DeviceRMA;

        constexpr RenderGraphResourcePool() noexcept = delete;
    };

    class RenderGraphPass final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphPass(const u32 passID, const u8 commandQueueIndex, const std::string_view& name, const ERenderGraphPassType passType,
                        RenderGraphSetupFunc&& setupFunc, RenderGraphExecuteFunc&& executeFunc) noexcept
            : m_ID(passID), m_CommandQueueIndex(commandQueueIndex), m_Name(name), m_Type(passType), m_SetupFunc(setupFunc),
              m_ExecuteFunc(executeFunc)
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

            if (m_Type == ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS)
            {
                RDNT_ASSERT(m_Viewport.has_value(), "Viewport is invalid!");
                RDNT_ASSERT(m_Scissor.has_value(), "Scissor is invalid!");

                commandBuffer.setViewportWithCount(*m_Viewport);
                commandBuffer.setScissorWithCount(*m_Scissor);
            }

            m_ExecuteFunc(resourceScheduler, commandBuffer);
        }

      private:
        ERenderGraphPassType m_Type{ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS};
        u8 m_CommandQueueIndex{0};
        u8 m_RenderTargetCount{0};
        u32 m_ID{0};
        u32 m_DependencyLevelIndex{0};
        std::string m_Name{s_DEFAULT_STRING};
        //    UnorderedSet<u32> m_PassesToSyncWith;  // Contains pass ID.

        RenderGraphSetupFunc m_SetupFunc{};
        RenderGraphExecuteFunc m_ExecuteFunc{};

        UnorderedMap<RenderGraphSubresourceID, ResourceStateFlags> m_ResourceIDToResourceState;

        std::vector<RenderGraphSubresourceID> m_TextureReads;
        std::vector<RenderGraphSubresourceID> m_TextureWrites;

        std::vector<RenderGraphSubresourceID> m_BufferReads;
        std::vector<RenderGraphSubresourceID> m_BufferWrites;

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

        std::array<RenderTargetInfo, s_MaxColorRenderTargets> m_RenderTargetInfos;
        std::optional<DepthStencilInfo> m_DepthStencilInfo{std::nullopt};
        std::optional<vk::Viewport> m_Viewport{std::nullopt};
        std::optional<vk::Rect2D> m_Scissor{std::nullopt};

        // NOTE: Now only for buffers, texture support will be added as needed.
        struct ClearOnExecuteData
        {
            RGResourceID ResourceID{};
            u32 Data{};
            u64 Size{};
            u64 Offset{};
        };
        std::vector<ClearOnExecuteData> m_ClearsOnExecute;

        friend RenderGraph;
        friend RenderGraph::DependencyLevel;
        friend RenderGraphResourceScheduler;
        constexpr RenderGraphPass() noexcept = delete;
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

        // NOTE: Should be called only inside pass that also writes to resource!
        void ClearOnExecute(const std::string& name, const u32 data, const u64 size, const u64 offset = 0) noexcept;

        NODISCARD RGResourceID ReadTexture(const std::string& name, const MipSet& mipSet, const ResourceStateFlags resourceState) noexcept;
        NODISCARD RGResourceID WriteTexture(const std::string& name, const MipSet& mipSet, const ResourceStateFlags resourceState,
                                            const std::string& newAliasName = s_DEFAULT_STRING) noexcept;
        void WriteDepthStencil(const std::string& name, const MipSet& mipSet, const vk::AttachmentLoadOp depthLoadOp,
                               const vk::AttachmentStoreOp depthStoreOp, const vk::ClearDepthStencilValue& clearValue,
                               const vk::AttachmentLoadOp stencilLoadOp   = vk::AttachmentLoadOp::eDontCare,
                               const vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                               const std::string& newAliasName            = s_DEFAULT_STRING) noexcept;
        void WriteRenderTarget(const std::string& name, const MipSet& mipSet, const vk::AttachmentLoadOp loadOp,
                               const vk::AttachmentStoreOp storeOp, const vk::ClearColorValue& clearValue,
                               const std::string& newAliasName = s_DEFAULT_STRING) noexcept;

        void CreateBuffer(const std::string& name, const GfxBufferDescription& bufferDesc) noexcept;
        NODISCARD FORCEINLINE Unique<GfxBuffer>& GetBuffer(const RGResourceID& resourceID) const noexcept
        {
            return m_RenderGraph.GetBuffer(resourceID);
        }

        NODISCARD RGResourceID ReadBuffer(const std::string& name, const ResourceStateFlags resourceState) noexcept;
        NODISCARD RGResourceID WriteBuffer(const std::string& name, const ResourceStateFlags resourceState) noexcept;

        FORCEINLINE void SetViewportScissors(const vk::Viewport& viewport, const vk::Rect2D& scissor) noexcept
        {
            m_Pass.m_Viewport = viewport;
            m_Pass.m_Scissor  = scissor;
        }

      private:
        RenderGraph& m_RenderGraph;
        RenderGraphPass& m_Pass;

        friend class RenderGraph;
        constexpr RenderGraphResourceScheduler() noexcept = delete;
        RenderGraphResourceScheduler(RenderGraph& renderGraph, RenderGraphPass& pass) noexcept : m_RenderGraph(renderGraph), m_Pass(pass) {}
    };

}  // namespace Radiant
