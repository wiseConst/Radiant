#pragma once

#include <Render/CoreDefines.hpp>
#include <vulkan/vulkan.hpp>

namespace Radiant
{

    class GfxBuffer;
    class GfxContext;
    class GfxTexture;

    struct RenderObject final
    {
        glm::mat4 TRS{1.0f};
        Shared<GfxBuffer> VertexPositionBuffer{nullptr};
        Shared<GfxBuffer> VertexAttributeBuffer{nullptr};
        Shared<GfxBuffer> IndexBuffer{nullptr};
        u32 IndexCount{0};
        u32 FirstIndex{0};
        Shared<GfxBuffer> MaterialBuffer{nullptr};
        vk::PrimitiveTopology PrimitiveTopology{vk::PrimitiveTopology::ePointList};
        vk::CullModeFlags CullMode{vk::CullModeFlagBits::eBack};
        EAlphaMode AlphaMode{EAlphaMode::ALPHA_MODE_OPAQUE};
    };

    struct DrawContext final
    {
        std::vector<RenderObject> RenderObjects;
    };

    struct GeometryData final
    {
        u32 StartIndex{};
        u32 Count{};
        Sphere Bounds{};
        u32 MaterialID{};
        vk::PrimitiveTopology PrimitiveTopology{vk::PrimitiveTopology::eTriangleList};
        vk::CullModeFlags CullMode{vk::CullModeFlagBits::eBack};
        EAlphaMode AlphaMode{EAlphaMode::ALPHA_MODE_OPAQUE};
    };

    struct MeshAsset final
    {
        std::string Name{s_DEFAULT_STRING};
        std::vector<GeometryData> Surfaces;
        u32 IndexBufferID{};
        u32 VertexPositionBufferID{};
        u32 VertexAttributeBufferID{};
    };

    struct RenderNode final
    {
        std::string Name{s_DEFAULT_STRING};
        WeakPtr<RenderNode> Parent{};
        std::vector<Shared<RenderNode>> Children;

        Shared<MeshAsset> MeshAsset{nullptr};

        glm::mat4 LocalTransform{1.f};
        glm::mat4 WorldTransform{1.f};

        void Iterate(DrawContext& drawContext, const std::vector<Shared<GfxBuffer>>& vertexPositionBuffers,
                     const std::vector<Shared<GfxBuffer>>& vertexAttributeBuffers, const std::vector<Shared<GfxBuffer>>& indexBuffers,
                     const std::vector<Shared<GfxBuffer>>& materialBuffers, const glm::mat4& topMatrix) noexcept
        {
            if (MeshAsset)
            {
                const auto& indexBuffer        = indexBuffers[MeshAsset->IndexBufferID];
                const auto& vertexPosBuffer    = vertexPositionBuffers[MeshAsset->VertexPositionBufferID];
                const auto& vertexAttribBuffer = vertexAttributeBuffers[MeshAsset->VertexAttributeBufferID];
                const auto& modelMatrix        = topMatrix * WorldTransform;

                for (const auto& surface : MeshAsset->Surfaces)
                {
                    const auto& materialBuffer = materialBuffers[surface.MaterialID];
                    drawContext.RenderObjects.emplace_back(modelMatrix, vertexPosBuffer, vertexAttribBuffer, indexBuffer, surface.Count,
                                                           surface.StartIndex, materialBuffer, surface.PrimitiveTopology, surface.CullMode,
                                                           surface.AlphaMode);
                }
            }

            for (const auto& child : Children)
            {
                child->Iterate(drawContext, vertexPositionBuffers, vertexAttributeBuffers, indexBuffers, materialBuffers, topMatrix);
            }
        }

        void RefreshTransform(const glm::mat4& parentTransform) noexcept
        {
            WorldTransform = parentTransform * LocalTransform;
            for (auto& child : Children)
                child->RefreshTransform(WorldTransform);
        }
    };

    struct Mesh final
    {
        Mesh(const Unique<GfxContext>& gfxContext, const std::filesystem::path& meshFilePath) noexcept;
        ~Mesh() noexcept = default;

        std::vector<Shared<RenderNode>> RootNodes;
        UnorderedMap<std::string, Shared<RenderNode>> RenderNodes;
        UnorderedMap<std::string, Shared<GfxTexture>> TextureMap;
        UnorderedMap<std::string, Shared<MeshAsset>> MeshAssetMap;
        std::vector<Shared<GfxBuffer>> VertexPositionBuffers;
        std::vector<Shared<GfxBuffer>> VertexAttributeBuffers;
        std::vector<Shared<GfxBuffer>> IndexBuffers;
        std::vector<Shared<GfxBuffer>> MaterialBuffers;
    };

}  // namespace Radiant
