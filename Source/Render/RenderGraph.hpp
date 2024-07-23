#pragma once

#include <Render/CoreDefines.hpp>

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>

#include <variant>
#include <vector>
#include <array>
#include <functional>

namespace Radiant
{
    class RenderGraph;
    class RenderGraphPass;
    class RenderGraphResourcePool;
    class RenderGraphResourceScheduler;

    enum class ERenderGraphPassType : std::uint8_t
    {
        RENDER_GRAPH_PASS_TYPE_GRAPHICS,
        RENDER_GRAPH_PASS_TYPE_TRANSFER,
        RENDER_GRAPH_PASS_TYPE_COMPUTE,
        RENDER_GRAPH_PASS_TYPE_RAY_TRACING
        // TODO:  RENDER_GRAPH_PASS_TYPE_ASYNC_COMPUTE,
        // TODO:   RENDER_GRAPH_PASS_TYPE_DEDICATED_TRANSFER,
    };

    using RenderGraphSetupFunc   = std::function<void(RenderGraphResourceScheduler&)>;
    using RenderGraphExecuteFunc = std::function<void(RenderGraphResourceScheduler&, const vk::CommandBuffer&)>;

    // RenderGraphResourcePool
    struct RenderGraphBufferHandle
    {
        std::uint64_t ID{0};
        ExtraBufferFlags BufferFlags{EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED};
    };

    using RGTextureHandle = std::uint64_t;
    using RGBufferHandle  = RenderGraphBufferHandle;

    // RenderGraph
    using RGResourceID = std::uint64_t;

    class RenderGraph final : private Uncopyable, private Unmovable
    {
      public:
        explicit RenderGraph(Unique<GfxContext>& gfxContext, const std::string_view& name,
                             Unique<RenderGraphResourcePool>& resourcePool) noexcept
            : m_GfxContext(gfxContext), m_Name(name), m_ResourcePool(resourcePool)
        {
            RDNT_ASSERT(gfxContext && resourcePool, "GfxContext or RenderGraphResourcePool is invalid!");
        }
        ~RenderGraph() noexcept = default;

        // TODO: Utilize dependency levels
        // Passes inside a dependency level can be executed in parallel (hope so)
        class DependencyLevel final
        {
          public:
            DependencyLevel(RenderGraph& renderGraph) noexcept : m_RenderGraph(renderGraph) {}
            ~DependencyLevel() noexcept = default;

            void AddPassIndex(const std::uint32_t passIndex) noexcept { m_PassIndices.emplace(passIndex); }

            void Execute(const vk::CommandBuffer& cmd) noexcept;

          private:
            RenderGraph& m_RenderGraph;
            std::uint32_t m_LevelIndex{0};
            UnorderedSet<std::uint32_t> m_PassIndices;

            friend RenderGraph;
        };

        void AddPass(const std::string_view& name, const ERenderGraphPassType passType, RenderGraphSetupFunc&& setupFunc,
                     RenderGraphExecuteFunc&& executeFunc) noexcept;

        // Compiles the graph and executes it best way.
        void Execute() noexcept;

        RGResourceID CreateResourceID(const std::string& name) noexcept
        {
            RDNT_ASSERT(!name.empty(), "Resource name is empty!");
            RDNT_ASSERT(!m_ResourceNameToID.contains(name), "Resource[{}] already exists!", name);

            return m_ResourceNameToID[name] = m_ResourceIDs.Emplace(m_ResourceIDs.GetSize());
        }

        NODISCARD RGResourceID GetResourceID(const std::string& name) noexcept
        {
            RDNT_ASSERT(!name.empty(), "Resource name is empty!");
            RDNT_ASSERT(m_ResourceNameToID.contains(name), "Resource[{}] doesn't exist!", name);

            return m_ResourceNameToID[name];
        }

        NODISCARD Unique<GfxTexture>& GetTexture(const RGResourceID& resourceID) noexcept;

      private:
        Unique<GfxContext>& m_GfxContext;
        std::string m_Name{s_DEFAULT_STRING};
        Unique<RenderGraphResourcePool>& m_ResourcePool;

        std::vector<Unique<RenderGraphPass>> m_Passes;
        std::vector<std::uint32_t> m_SortedPassID;
        std::vector<std::vector<std::uint32_t>> m_AdjacencyLists;

        std::vector<DependencyLevel> m_DependencyLevels;

        Pool<RGResourceID> m_ResourceIDs;
        UnorderedMap<std::string, RGResourceID> m_ResourceNameToID;
        UnorderedMap<std::string, std::string> m_AliasMap;  // For RMW things.

        UnorderedMap<RGResourceID, RGTextureHandle> m_ResourceIDToTextureHandle;
        UnorderedMap<RGResourceID, RGBufferHandle> m_ResourceIDToBufferHandle;

        friend DependencyLevel;
        constexpr RenderGraph() noexcept = delete;
        void Compile() noexcept;
        void BuildAdjacencyLists() noexcept;
        void TopologicalSort() noexcept;
        void BuildDependencyLevels() noexcept;

        void GraphvizDump() const noexcept;
    };

    // struct RenderGraphResourceTimeline
    //{
    //     std::uint32_t BeginPass;
    //     std::uint32_t EndPass;
    // };
    // std::vector<std::pair<std::uint32_t, std::uint32_t>>;

    template <typename TResource> class RenderGraphResource final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphResource(Unique<TResource> resource) noexcept : m_Handle(std::move(resource)) {}
        ~RenderGraphResource() noexcept = default;

        NODISCARD FORCEINLINE auto& Get() noexcept { return m_Handle; }
        NODISCARD FORCEINLINE const auto GetState() const noexcept { return m_CurrentState; }
        FORCEINLINE void SetState(const ResourceStateFlags resourceState) noexcept { m_CurrentState |= resourceState; }

      private:
        Unique<TResource> m_Handle{nullptr};
        ResourceStateFlags m_CurrentState{EResourceState::RESOURCE_STATE_UNDEFINED};

        // UnorderedSet<std::uint32_t> m_ReadPasses;
        // UnorderedSet<std::uint32_t> m_WritePasses;
    };

    using RenderGraphResourceTexture = RenderGraphResource<GfxTexture>;
    using RenderGraphResourceBuffer  = RenderGraphResource<GfxBuffer>;

    // NOTE:
    // 1) All CPU-side buffers are buffered by default!
    // 2) All textures aren't buffered!
    class RenderGraphResourcePool final : private Uncopyable, private Unmovable
    {
      public:
        RenderGraphResourcePool(const Unique<GfxDevice>& device) noexcept : m_Device(device){};
        ~RenderGraphResourcePool() noexcept = default;

        void Tick() noexcept
        {
            m_CurrentFrameNumber = (m_CurrentFrameNumber + 1) % s_BufferedFrameCount;
            ++m_GlobalFrameNumber;

            for (std::uint32_t i{}; i < m_Textures.size();)
            {
                const std::uint64_t framesPast = m_Textures[i].LastUsedFrameIndex + static_cast<std::uint64_t>(s_BufferedFrameCount);
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

            for (std::uint32_t i{}; i < m_DeviceBuffers.size();)
            {
                const std::uint64_t framesPast = m_DeviceBuffers[i].LastUsedFrameIndex + static_cast<std::uint64_t>(s_BufferedFrameCount);
                if (framesPast < m_GlobalFrameNumber)
                {
                    std::swap(m_DeviceBuffers[i], m_DeviceBuffers.back());
                    m_DeviceBuffers.pop_back();
                }
                else
                    ++i;
            }

            auto& currentHostBuffersVector = m_HostBuffers[m_CurrentFrameNumber];
            for (std::uint32_t i{}; i < currentHostBuffersVector.size();)
            {
                const std::uint64_t framesPast =
                    currentHostBuffersVector[i].LastUsedFrameIndex + static_cast<std::uint64_t>(s_BufferedFrameCount);
                if (framesPast < m_GlobalFrameNumber)
                {
                    std::swap(currentHostBuffersVector[i], currentHostBuffersVector.back());
                    currentHostBuffersVector.pop_back();
                }
                else
                    ++i;
            }
        }

        NODISCARD RGTextureHandle CreateTexture(const GfxTextureDescription& textureDesc) noexcept;
        NODISCARD RGBufferHandle CreateBuffer(const GfxBufferDescription& bufferDesc) noexcept;

        NODISCARD FORCEINLINE Unique<RenderGraphResourceTexture>& GetTexture(const RGTextureHandle& handleID) noexcept
        {
            RDNT_ASSERT(handleID < m_Textures.size(), "RGTextureHandle is invalid!");
            return m_Textures[handleID].Handle;
        }

        /* NODISCARD FORCEINLINE Unique<GfxBuffer>& GetBuffer(const RGBufferHandle& handleID) noexcept
         {
             if ((handleID.BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) ==
                 EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL)
             {
                 RDNT_ASSERT(handleID.ID < m_DeviceBuffers.size(), "GPUBufferHandle is invalid!");
                 return m_DeviceBuffers[handleID.ID].Handle->Get();
             }

             if ((handleID.BufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED)
             {
                 RDNT_ASSERT(handleID.ID < m_HostBuffers[m_CurrentFrameNumber].size(), "CPUBufferHandle is invalid!");
                 return m_HostBuffers[m_CurrentFrameNumber][handleID.ID].Handle->Get();
             }

             RDNT_ASSERT(false, "{}: Nothing to return!", __FUNCTION__);
         }*/

      private:
        const Unique<GfxDevice>& m_Device;
        std::uint64_t m_GlobalFrameNumber{0};
        std::uint8_t m_CurrentFrameNumber{0};

        struct PooledTexture
        {
            Unique<RenderGraphResourceTexture> Handle;
            std::uint64_t LastUsedFrameIndex{};
        };

        struct PooledBuffer
        {
            Unique<RenderGraphResourceBuffer> Handle;
            std::uint64_t LastUsedFrameIndex{};
        };

        using GfxBufferVector = std::vector<PooledBuffer>;

        std::array<GfxBufferVector, s_BufferedFrameCount> m_HostBuffers;
        GfxBufferVector m_DeviceBuffers;

        std::vector<PooledTexture> m_Textures;
        constexpr RenderGraphResourcePool() noexcept = delete;
    };

    class RenderGraphResourceScheduler final : private Uncopyable, private Unmovable
    {
      public:
        ~RenderGraphResourceScheduler() noexcept = default;

        void CreateTexture(const std::string& name, const GfxTextureDescription& textureDesc) noexcept;
        NODISCARD Unique<GfxTexture>& GetTexture(const RGResourceID& resourceID) noexcept;

        RGResourceID ReadTexture(const std::string& name, const ResourceStateFlags resourceState) noexcept;
        RGResourceID WriteTexture(const std::string& name, const ResourceStateFlags resourceState) noexcept;
        RGResourceID WriteDepthStencil(const std::string& name, const vk::ClearDepthStencilValue& clearValue) noexcept;
        RGResourceID WriteRenderTarget(const std::string& name, const vk::ClearColorValue& clearValue) noexcept;

        //   void CreateBuffer(const std::string& name, const GfxBufferDescription& bufferDesc) noexcept;

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
        RenderGraphPass(const std::uint32_t passID, const std::string_view& name, const ERenderGraphPassType passType,
                        RenderGraphSetupFunc&& setupFunc, RenderGraphExecuteFunc&& executeFunc) noexcept
            : m_ID(passID), m_Name(name), m_PassType(passType), m_SetupFunc(setupFunc), m_ExecuteFunc(executeFunc){};
        ~RenderGraphPass() noexcept = default;

        void Setup(RenderGraphResourceScheduler& resourceScheduler) const noexcept
        {
            RDNT_ASSERT(m_SetupFunc, "SetupFunc is invalid!");
            m_SetupFunc(resourceScheduler);
        }

        void ExecuteFunc(RenderGraphResourceScheduler& resourceScheduler, const vk::CommandBuffer& commandBuffer) const noexcept
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
        std::uint32_t m_ID{};
        ERenderGraphPassType m_PassType{ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS};
        std::uint32_t m_DependencyLevelIndex{};
        std::string m_Name{s_DEFAULT_STRING};

        RenderGraphSetupFunc m_SetupFunc{};
        RenderGraphExecuteFunc m_ExecuteFunc{};

        UnorderedMap<RGResourceID, ResourceStateFlags> m_ResourceIDToResourceState;
        UnorderedMap<std::string, GfxTextureDescription> m_TextureCreates;
        std::vector<RGResourceID> m_TextureReads;
        std::vector<RGResourceID> m_TextureWrites;

        /*       std::vector<RGBufferHandle> m_BufferReads;
                      std::vector<RGBufferHandle> m_BufferWrites;*/

        std::optional<vk::ClearColorValue> m_ColorClearValue{std::nullopt};
        std::optional<vk::ClearDepthStencilValue> m_DepthStencilClearValue{std::nullopt};
        std::optional<vk::Viewport> m_Viewport{std::nullopt};
        std::optional<vk::Rect2D> m_Scissor{std::nullopt};

        friend RenderGraph;
        friend RenderGraphResourceScheduler;
        constexpr RenderGraphPass() noexcept = delete;
    };

}  // namespace Radiant
